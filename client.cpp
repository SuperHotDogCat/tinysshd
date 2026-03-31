#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <termios.h>
#include <stdlib.h>
#include <getopt.h>
#include <netdb.h>

#define DEFAULT_PORT "2222"
#define DEFAULT_HOST "127.0.0.1"
#define BUFFER_SIZE 4096

struct termios orig_termios;
bool is_raw_mode = false;

void reset_terminal_mode() {
    if (is_raw_mode) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
        is_raw_mode = false;
    }
}

void set_raw_mode() {
    if (!is_raw_mode) {
        tcgetattr(STDIN_FILENO, &orig_termios);
        struct termios raw = orig_termios;
        cfmakeraw(&raw);
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
        is_raw_mode = true;
    }
}

void print_usage(char *prog_name) {
    std::cout << "Usage: " << prog_name << " [-h host] [-p port]" << std::endl;
    std::cout << "  -h host     Server host to connect to (default: " << DEFAULT_HOST << ")" << std::endl;
    std::cout << "  -p port     Server port to connect to (default: " << DEFAULT_PORT << ")" << std::endl;
}

int main(int argc, char *argv[]) {
    std::string host = DEFAULT_HOST;
    std::string port = DEFAULT_PORT;

    int opt;
    while ((opt = getopt(argc, argv, "h:p:?")) != -1) {
        switch (opt) {
            case 'h':
                host = optarg;
                break;
            case 'p':
                port = optarg;
                break;
            case '?':
            default:
                print_usage(argv[0]);
                return 0;
        }
    }

    atexit(reset_terminal_mode);

    struct addrinfo hints, *servinfo, *p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int rv;
    if ((rv = getaddrinfo(host.c_str(), port.c_str(), &hints, &servinfo)) != 0) {
        std::cerr << "getaddrinfo: " << gai_strerror(rv) << std::endl;
        return 1;
    }

    int sock;
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }

        if (connect(sock, p->ai_addr, p->ai_addrlen) == -1) {
            close(sock);
            perror("client: connect");
            continue;
        }

        break;
    }

    if (p == NULL) {
        std::cerr << "client: failed to connect" << std::endl;
        freeaddrinfo(servinfo);
        return 2;
    }

    freeaddrinfo(servinfo);

    std::cout << "Connected to " << host << ":" << port << std::endl;

    // Password authentication phase (in cooked mode)
    char buffer[BUFFER_SIZE];
    while (true) {
        ssize_t n = read(sock, buffer, sizeof(buffer) - 1);
        if (n <= 0) break;
        buffer[n] = '\0';
        std::cout << buffer << std::flush;

        if (strstr(buffer, "Password: ")) {
            std::string pwd;
            std::getline(std::cin, pwd);
            pwd += "\n";
            write(sock, pwd.c_str(), pwd.length());
        } else if (strstr(buffer, "Authentication successful.")) {
            break;
        } else if (strstr(buffer, "Authentication failed.")) {
            return 1;
        }
    }

    std::cout << "Switching to raw mode..." << std::endl;
    set_raw_mode();

    fd_set read_fds;
    while (true) {
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(sock, &read_fds);

        if (select(sock + 1, &read_fds, NULL, NULL, NULL) < 0) {
            perror("select");
            break;
        }

        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            ssize_t n = read(STDIN_FILENO, buffer, sizeof(buffer));
            if (n <= 0) break;
            if (write(sock, buffer, n) != (ssize_t)n) break;
        }

        if (FD_ISSET(sock, &read_fds)) {
            ssize_t n = read(sock, buffer, sizeof(buffer));
            if (n <= 0) break;
            if (write(STDOUT_FILENO, buffer, n) != (ssize_t)n) break;
        }
    }

    close(sock);
    return 0;
}
