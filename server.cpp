#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <termios.h>
#include <stdlib.h>

#define PORT 2222
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

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(server_fd, 3) < 0) {
        perror("listen");
        return 1;
    }

    std::cout << "Server listening on port " << PORT << std::endl;

    while (true) {
        struct sockaddr_in client_address;
        socklen_t addrlen = sizeof(client_address);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_address, &addrlen);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        std::cout << "Client connected" << std::endl;
        handle_client(client_fd);
        close(client_fd);
        std::cout << "Client disconnected" << std::endl;
    }

    close(server_fd);
    return 0;
}
