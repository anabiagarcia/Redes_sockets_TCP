
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
    int sockfd;
    int running = 1;
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    socklen_t client_len;
    char buffer[BUFFER_SIZE];

    if (argc != 2) {
        fprintf(stderr, "Uso: %s <porta>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int port = atoi(argv[1]);

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    if (sockfd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == -1) {
        perror("bind");
        close(sockfd);
        return EXIT_FAILURE;
    }

    printf("Servidor UDP aguardando na porta %d...\n", port);

    while (running) {
        client_len = sizeof(client_addr);

        ssize_t n = recvfrom(
            sockfd,
            buffer,
            sizeof(buffer),
            0,
            (struct sockaddr *)&client_addr,
            &client_len
        );

        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }

            perror("recvfrom");
            running = 0;
        } else {
            printf("Datagrama de %s:%u (%zd bytes)\n",
                   inet_ntoa(client_addr.sin_addr),
                   (unsigned)ntohs(client_addr.sin_port),
                   n);

            printf("Conteudo: ");
            fwrite(buffer, 1, (size_t)n, stdout);
            putchar('\n');

            ssize_t sent = sendto(
                sockfd,
                buffer,
                (size_t)n,
                0,
                (struct sockaddr *)&client_addr,
                client_len
            );

            if (sent == -1) {
                perror("sendto");
            } else if (sent != n) {
                fprintf(stderr,
                        "Datagrama enviado parcialmente.\n");
            }
        }
    }

    close(sockfd);
    printf("Servidor encerrado.\n");

    return EXIT_SUCCESS;
}
