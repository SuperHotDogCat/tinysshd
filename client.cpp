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

#define DEFAULT_PORT 2222
#define DEFAULT_HOST "127.0.0.1"
#define BUFFER_SIZE 4096

struct termios orig_termios;

void reset_terminal_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void set_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(reset_terminal_mode);

    struct termios raw = orig_termios;
    cfmakeraw(&raw);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void print_usage(char *prog_name) {
    std::cout << "Usage: " << prog_name << " [-h host] [-p port]" << std::endl;
    std::cout << "  -h host     Server host to connect to (default: " << DEFAULT_HOST << ")" << std::endl;
    std::cout << "  -p port     Server port to connect to (default: " << DEFAULT_PORT << ")" << std::endl;
}

int main(int argc, char *argv[]) {
    std::string host = DEFAULT_HOST;
    int port = DEFAULT_PORT;

    int opt;
    while ((opt = getopt(argc, argv, "h:p:?")) != -1) {
        switch (opt) {
            case 'h':
                host = optarg;
                break;
            case 'p':
                port = atoi(optarg);
                break;
            case '?':
            default:
                print_usage(argv[0]);
                return 0;
        }
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &serv_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address/ Address not supported" << std::endl;
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        return 1;
    }

    std::cout << "Connected to " << host << ":" << port << std::endl;
    std::cout << "Switching to raw mode..." << std::endl;

    set_raw_mode();

    fd_set read_fds;
    char buffer[BUFFER_SIZE];

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
            if (write(sock, buffer, n) != n) break;
        }

        if (FD_ISSET(sock, &read_fds)) {
            ssize_t n = read(sock, buffer, sizeof(buffer));
            if (n <= 0) break;
            if (write(STDOUT_FILENO, buffer, n) != n) break;
        }
    }

    close(sock);
    return 0;
}
