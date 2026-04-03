#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <termios.h>
#include <stdlib.h>
#include <getopt.h>
#include <signal.h>
#include <algorithm>
#include <openssl/ssl.h>
#include <openssl/err.h>

#define DEFAULT_PORT 2222
#define DEFAULT_ADDR "0.0.0.0"
#define BUFFER_SIZE 4096

std::string global_password;

// Clean up zombie processes
void sigchld_handler(int) {
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0);
    errno = saved_errno;
}

std::string generate_password(int length = 12) {
    const char charset[] = "0123456789"
                           "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                           "abcdefghijklmnopqrstuvwxyz";
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        perror("open /dev/urandom");
        return "fallback123";
    }

    std::string password;
    for (int i = 0; i < length; ++i) {
        unsigned char c;
        if (read(fd, &c, 1) != 1) break;
        password += charset[c % (sizeof(charset) - 1)];
    }
    close(fd);
    return password;
}

bool authenticate(SSL* ssl, int client_fd) {
    const char *prompt = "Password: ";
    SSL_write(ssl, prompt, strlen(prompt));

    std::string input;
    char c;
    while (true) {
        ssize_t n = SSL_read(ssl, &c, 1);
        if (n <= 0) return false;
        if (c == '\n' || c == '\r') break;
        input += c;
        if (input.length() > 128) return false; // Prevent buffer overflow
    }

    if (input == global_password) {
        const char *msg = "Authentication successful.\n";
        SSL_write(ssl, msg, strlen(msg));
        return true;
    } else {
        const char *msg = "Authentication failed.\n";
        SSL_write(ssl, msg, strlen(msg));
        return false;
    }
}

void handle_client(SSL* ssl, int client_fd, int server_fd) {
    if (!authenticate(ssl, client_fd)) {
        close(client_fd);
        close(server_fd);
        exit(0);
    }

    int master_fd = posix_openpt(O_RDWR);
    if (master_fd < 0) {
        perror("posix_openpt");
        return;
    }

    if (grantpt(master_fd) < 0) {
        perror("grantpt");
        close(master_fd);
        return;
    }

    if (unlockpt(master_fd) < 0) {
        perror("unlockpt");
        close(master_fd);
        return;
    }

    char *slave_name = ptsname(master_fd);
    if (slave_name == NULL) {
        perror("ptsname");
        close(master_fd);
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        close(master_fd);
        return;
    }

    if (pid == 0) {
        // Child process (PTY slave side)
        close(client_fd);
        close(server_fd);
        setsid();

        int slave_fd = open(slave_name, O_RDWR);
        if (slave_fd < 0) {
            perror("open slave");
            exit(1);
        }

        // Close master fd in child
        close(master_fd);

        // Redirect stdin, stdout, stderr to slave PTY
        dup2(slave_fd, STDIN_FILENO);
        dup2(slave_fd, STDOUT_FILENO);
        dup2(slave_fd, STDERR_FILENO);

        if (slave_fd > STDERR_FILENO) {
            close(slave_fd);
        }

        execlp("/bin/bash", "/bin/bash", NULL);
        perror("execlp");
        exit(1);
    } else {
        // Parent process (Proxy loop)
        close(server_fd);
        fd_set read_fds;
        char buffer[BUFFER_SIZE];

        while (true) {
            FD_ZERO(&read_fds);
            FD_SET(client_fd, &read_fds);
            FD_SET(master_fd, &read_fds);

            int max_fd = (client_fd > master_fd) ? client_fd : master_fd;

            if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
                if (errno == EINTR) continue;
                perror("select");
                break;
            }

            if (FD_ISSET(client_fd, &read_fds)) {
                // ssize_t n = read(client_fd, buffer, sizeof(buffer));
                ssize_t n = SSL_read(ssl, buffer, sizeof(buffer));
                if (n <= 0) break;
                if (write(master_fd, buffer, n) != n) break;
            }

            if (FD_ISSET(master_fd, &read_fds)) {
                ssize_t n = read(master_fd, buffer, sizeof(buffer));
                if (n <= 0) break;
                if (SSL_write(ssl, buffer, n) != n) break;
            }
        }

        close(master_fd);
        close(client_fd);
        waitpid(pid, NULL, 0);
        exit(0);
    }
}

void print_usage(char *prog_name) {
    std::cout << "Usage: " << prog_name << " [-p port] [-a address]" << std::endl;
    std::cout << "  -p port     Port to listen on (default: " << DEFAULT_PORT << ")" << std::endl;
    std::cout << "  -a address  Address to bind to (default: " << DEFAULT_ADDR << ")" << std::endl;
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    std::string bind_addr = DEFAULT_ADDR;

    int opt;
    while ((opt = getopt(argc, argv, "p:a:h")) != -1) {
        switch (opt) {
            case 'p':
                port = atoi(optarg);
                break;
            case 'a':
                bind_addr = optarg;
                break;
            case 'h':
            default:
                print_usage(argv[0]);
                return 0;
        }
    }

    global_password = generate_password();
    std::cout << "------------------------------------------" << std::endl;
    std::cout << "Generated password: " << global_password << std::endl;
    std::cout << "------------------------------------------" << std::endl;

    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        return 1;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt_sock = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt_sock, sizeof(opt_sock));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    if (inet_aton(bind_addr.c_str(), &address.sin_addr) == 0) {
        std::cerr << "Invalid bind address: " << bind_addr << std::endl;
        return 1;
    }
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(server_fd, 5) < 0) {
        perror("listen");
        return 1;
    }

    std::cout << "Server listening on " << bind_addr << ":" << port << std::endl;

    // SSL設定
    // TODO: Error処理
    SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());
    // 証明書設定
    SSL_CTX_use_certificate_file(ctx, "cert.pem", SSL_FILETYPE_PEM);
    SSL_CTX_use_PrivateKey_file(ctx, "key.pem", SSL_FILETYPE_PEM);

    while (true) {
        struct sockaddr_in client_address;
        socklen_t addrlen = sizeof(client_address);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_address, &addrlen);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }
        SSL* ssl = SSL_new(ctx);
        SSL_set_fd(ssl, client_fd);
        // SSL_connectがclient側にあって初めてハンドシェイク成立
        if (SSL_accept(ssl) <= 0) {
            ERR_print_errors_fp(stderr);
            exit(1);
        }

        std::cout << "Client connected from " << inet_ntoa(client_address.sin_addr) << std::endl;

        pid_t pid = fork();
        if (pid == 0) {
            handle_client(ssl, client_fd, server_fd);
        } else if (pid > 0) {
            close(client_fd);
        } else {
            perror("fork");
            close(client_fd);
        }
    }

    close(server_fd);
    return 0;
}
