
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

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

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

            ssize_t sent = sendto(
                sockfd,
                buffer,
                len,
                0,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)
            );

            if (sent == -1) {
                if (errno == EINTR) {
                    continue;
                }

                perror("sendto");
                running = 0;
            } else if ((size_t)sent != len) {
                fprintf(stderr,
                        "Datagrama enviado parcialmente.\n");
                running = 0;
            } else {
                struct sockaddr_in reply_addr;
                socklen_t reply_len = sizeof(reply_addr);

                ssize_t n = recvfrom(
                    sockfd,
                    buffer,
                    sizeof(buffer),
                    0,
                    (struct sockaddr *)&reply_addr,
                    &reply_len
                );

                if (n == -1) {
                    if (errno == EINTR) {
                        continue;
                    }

                    perror("recvfrom");
                    running = 0;
                } else if (
                    reply_addr.sin_family != AF_INET ||
                    reply_addr.sin_addr.s_addr !=
                        server_addr.sin_addr.s_addr ||
                    reply_addr.sin_port != server_addr.sin_port
                ) {
                    printf("Datagrama de origem inesperada.\n");
                } else {
                    printf("Resposta: ");
                    fwrite(buffer, 1, (size_t)n, stdout);
                    putchar('\n');
                }
            }
        }
    }

    close(sockfd);
    printf("Cliente encerrado.\n");

    return EXIT_SUCCESS;
}
