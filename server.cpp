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

#define DEFAULT_PORT 2222
#define DEFAULT_ADDR "0.0.0.0"
#define BUFFER_SIZE 4096

void handle_client(int client_fd) {
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
        // Child process
        close(client_fd);
        // Note: server_fd is not accessible here easily without passing it,
        // but in a real app we should close all inherited FDs except the ones we need.
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
        // Parent process
        fd_set read_fds;
        char buffer[BUFFER_SIZE];

        while (true) {
            FD_ZERO(&read_fds);
            FD_SET(client_fd, &read_fds);
            FD_SET(master_fd, &read_fds);

            int max_fd = (client_fd > master_fd) ? client_fd : master_fd;

            if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
                perror("select");
                break;
            }

            if (FD_ISSET(client_fd, &read_fds)) {
                ssize_t n = read(client_fd, buffer, sizeof(buffer));
                if (n <= 0) break;
                if (write(master_fd, buffer, n) != n) break;
            }

            if (FD_ISSET(master_fd, &read_fds)) {
                ssize_t n = read(master_fd, buffer, sizeof(buffer));
                if (n <= 0) break;
                if (write(client_fd, buffer, n) != n) break;
            }
        }

        close(master_fd);
        waitpid(pid, NULL, 0);
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

    if (listen(server_fd, 3) < 0) {
        perror("listen");
        return 1;
    }

    std::cout << "Server listening on " << bind_addr << ":" << port << std::endl;

    while (true) {
        struct sockaddr_in client_address;
        socklen_t addrlen = sizeof(client_address);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_address, &addrlen);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        std::cout << "Client connected from " << inet_ntoa(client_address.sin_addr) << std::endl;
        handle_client(client_fd);
        close(client_fd);
        std::cout << "Client disconnected" << std::endl;
    }

    close(server_fd);
    return 0;
}
