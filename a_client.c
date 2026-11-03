
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
    int sockfd;
    int running = 1;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];

    if (argc != 3) {
        fprintf(stderr, "Uso: %s <IP> <porta>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int port = atoi(argv[2]);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, argv[1],
                  &server_addr.sin_addr) != 1) {
        fprintf(stderr, "Endereco IPv4 invalido.\n");
        close(sockfd);
        return EXIT_FAILURE;
    }

    if (connect(sockfd, (struct sockaddr *)&server_addr,
                sizeof(server_addr)) == -1) {
        perror("connect");
        close(sockfd);
        return EXIT_FAILURE;
    }

    while (running) {
        printf("Mensagem (/quit para sair): ");
        fflush(stdout);

        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
            running = 0;
        } else if (strcmp(buffer, "/quit\n") == 0 ||
                   strcmp(buffer, "/quit") == 0) {
            running = 0;
        } else {
            size_t len = strlen(buffer);
            size_t sent = 0;

            while (sent < len && running) {
                ssize_t n = send(
                    sockfd,
                    buffer + sent,
                    len - sent,
                    0
                );

                if (n == -1) {
                    if (errno == EINTR) {
                        continue;
                    }

                    perror("send");
                    running = 0;
                } else if (n == 0) {
                    running = 0;
                } else {
                    sent += (size_t)n;
                }
            }

            size_t received = 0;

            while (received < sent && running) {
                ssize_t n = recv(
                    sockfd,
                    buffer + received,
                    sent - received,
                    0
                );

                if (n == -1) {
                    if (errno == EINTR) {
                        continue;
                    }

                    perror("recv");
                    running = 0;
                } else if (n == 0) {
                    printf("Servidor desconectou.\n");
                    running = 0;
                } else {
                    received += (size_t)n;
                }
            }

            if (running) {
                printf("Resposta: ");
                fwrite(buffer, 1, received, stdout);

                if (received == 0 ||
                    buffer[received - 1] != '\n') {
                    putchar('\n');
                }
            }
        }
    }

    close(sockfd);

    printf("Cliente encerrado.\n");

    return EXIT_SUCCESS;
}
