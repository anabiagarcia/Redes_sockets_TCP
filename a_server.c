
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
    int server_fd, client_fd;
    int running = 1;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];

    if (argc != 2) {
        fprintf(stderr, "Uso: %s <porta>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int port = atoi(argv[1]);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }

    if (listen(server_fd, 5) == -1) {
        perror("listen");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("Servidor aguardando na porta %d...\n", port);

    client_fd = accept(server_fd, NULL, NULL);

    if (client_fd == -1) {
        perror("accept");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("Cliente conectado.\n");

    while (running) {
        ssize_t n = recv(client_fd, buffer,
                         sizeof(buffer), 0);

        if (n == 0) {
            printf("Cliente desconectado.\n");
            running = 0;
        } else if (n == -1) {
            if (errno == EINTR) {
                continue;
            }

            perror("recv");
            running = 0;
        } else {
            printf("Recebidos %zd bytes.\n", n);

            ssize_t sent = 0;

            while (sent < n) {
                ssize_t r = send(
                    client_fd,
                    buffer + sent,
                    (size_t)(n - sent),
                    0
                );

                if (r == -1) {
                    if (errno == EINTR) {
                        continue;
                    }

                    perror("send");
                    running = 0;
                    break;
                }

                if (r == 0) {
                    running = 0;
                    break;
                }

                sent += r;
            }
        }
    }

    close(client_fd);
    close(server_fd);

    printf("Servidor encerrado.\n");

    return EXIT_SUCCESS;
}
