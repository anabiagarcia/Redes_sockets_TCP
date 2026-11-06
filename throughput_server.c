#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUFFER_SIZE 65536

static void *handle_client(void *arg) {
    int fd = *(int *)arg;
    char buffer[BUFFER_SIZE];
    unsigned long long total_bytes = 0;

    free(arg);

    // TCP pode entregar menos bytes do que o tamanho do buffer.
    for (;;) {
        ssize_t n = recv(fd, buffer, sizeof(buffer), 0);

        if (n == 0) {
            printf("Conexao %d: %llu bytes recebidos\n", fd, total_bytes);
            fflush(stdout);
            break;
        }

        if (n < 0) {
            if (errno == EINTR)
                continue;

            perror("recv");
            break;
        }

        total_bytes += (unsigned long long)n;
    }

    close(fd);
    return NULL;
}

int main(int argc, char *argv[]) {
    char *end;
    long port;
    int reuse = 1;
    pthread_attr_t attr;

    if (argc != 2) {
        fprintf(stderr, "Uso: %s <porta>\n", argv[0]);
        return EXIT_FAILURE;
    }

    errno = 0;
    port = strtol(argv[1], &end, 10);
    if (errno != 0 || end == argv[1] || *end != '\0' ||
        port < 1 || port > 65535) {
        fprintf(stderr, "Porta invalida: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((unsigned short)port);

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &reuse, sizeof(reuse)) == -1 ||
        bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == -1 ||
        listen(server_fd, 10) == -1) {
        perror("Preparacao do socket de escuta");
        close(server_fd);
        return EXIT_FAILURE;
    }

    if (pthread_attr_init(&attr) != 0) {
        fprintf(stderr, "Erro ao inicializar atributos da thread.\n");
        close(server_fd);
        return EXIT_FAILURE;
    }

    if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0) {
        fprintf(stderr, "Erro ao configurar atributos da thread.\n");
        pthread_attr_destroy(&attr);
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("Servidor TCP ativo na porta %ld.\n", port);
    fflush(stdout);

    for (;;) {
        int fd = accept(server_fd, NULL, NULL);

        if (fd == -1) {
            if (errno == EINTR)
                continue;

            perror("accept");
            break;
        }

        int *client_fd = malloc(sizeof(*client_fd));
        if (client_fd == NULL) {
            perror("malloc");
            close(fd);
            continue;
        }

        *client_fd = fd;
        pthread_t thread;
        if (pthread_create(&thread, &attr, handle_client, client_fd) != 0) {
            fprintf(stderr, "Erro ao criar thread.\n");
            free(client_fd);
            close(fd);
        }
    }

    pthread_attr_destroy(&attr);
    close(server_fd);
    return EXIT_FAILURE;
}
