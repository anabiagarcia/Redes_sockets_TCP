#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define BUFFER_SIZE 65536
#define MAX_CLIENTS 10
#define MAX_WINDOW_SECONDS 3600
#define NS_PER_SECOND INT64_C(1000000000)

typedef struct {
    _Atomic int64_t start_ns;
    int64_t duration_ns;
} Window;

typedef struct {
    int fd;
    const Window *window;
    unsigned long long total_bytes;
    unsigned long long window_bytes;
    unsigned long long bytes_per_second[MAX_WINDOW_SECONDS];
    int64_t eof_ns;
    int failed;
} Client;

static int64_t now_ns(void) {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
        perror("clock_gettime");
        return -1;
    }

    return (int64_t)ts.tv_sec * NS_PER_SECOND + ts.tv_nsec;
}

static long parse_number(const char *text, long min, long max) {
    char *end;
    errno = 0;
    long value = strtol(text, &end, 10);

    if (errno != 0 || end == text || *end != '\0' ||
        value < min || value > max)
        return -1;

    return value;
}

static void *handle_client(void *arg) {
    Client *client = arg;
    char buffer[BUFFER_SIZE];
    unsigned long long total_bytes = 0;
    unsigned long long window_bytes = 0;

    // TCP pode entregar menos bytes do que o tamanho do buffer.
    for (;;) {
        ssize_t n = recv(client->fd, buffer, sizeof(buffer), 0);

        if (n < 0) {
            if (errno == EINTR)
                continue;

            perror("recv");
            client->failed = 1;
            break;
        }

        // Classifica o bloco pelo instante em que recv() termina.
        int64_t received_ns = now_ns();
        if (received_ns < 0) {
            client->failed = 1;
            break;
        }

        if (n == 0) {
            client->eof_ns = received_ns;
            break;
        }

        total_bytes += (unsigned long long)n;
        int64_t start_ns = atomic_load(&client->window->start_ns);
        int64_t elapsed_ns = received_ns - start_ns;
        if (start_ns != 0 && elapsed_ns >= 0 &&
            elapsed_ns < client->window->duration_ns) {
            window_bytes += (unsigned long long)n;
            client->bytes_per_second[elapsed_ns / NS_PER_SECOND] +=
                (unsigned long long)n;
        }
    }

    client->total_bytes = total_bytes;
    client->window_bytes = window_bytes;
    close(client->fd);
    return NULL;
}

int main(int argc, char *argv[]) {
    int reuse = 1;
    static Client clients[MAX_CLIENTS];
    pthread_t threads[MAX_CLIENTS];
    int started = 0;

    if (argc != 5) {
        fprintf(stderr, "Uso: %s <porta> <num_clientes> "
                        "<aquecimento_s> <janela_s>\n", argv[0]);
        return EXIT_FAILURE;
    }

    long expected = parse_number(argv[2], 1, MAX_CLIENTS);
    if (expected < 0) {
        fprintf(stderr, "Informe de 1 a %d clientes.\n", MAX_CLIENTS);
        return EXIT_FAILURE;
    }

    long port = parse_number(argv[1], 1, 65535);
    if (port < 0) {
        fprintf(stderr, "Porta invalida: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    long warmup = parse_number(argv[3], 1, 3600);
    long duration = parse_number(argv[4], 1, MAX_WINDOW_SECONDS);
    if (warmup < 0 || duration < 0) {
        fprintf(stderr, "Informe aquecimento e janela de 1 a 3600 segundos inteiros.\n");
        return EXIT_FAILURE;
    }

    Window window = {.start_ns = 0, .duration_ns = duration * NS_PER_SECOND};

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
        listen(server_fd, MAX_CLIENTS) == -1) {
        perror("Preparacao do socket de escuta");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("Servidor TCP ativo na porta %ld. Aguardando %ld cliente(s).\n",
           port, expected);
    fflush(stdout);

    while (started < expected) {
        int fd = accept(server_fd, NULL, NULL);

        if (fd == -1) {
            if (errno == EINTR)
                continue;

            perror("accept");
            break;
        }

        clients[started].fd = fd;
        clients[started].window = &window;
        if (pthread_create(&threads[started], NULL,
                           handle_client, &clients[started]) != 0) {
            fprintf(stderr, "Erro ao criar thread.\n");
            close(fd);
            break;
        }
        started++;
    }

    close(server_fd);

    int64_t end_ns = 0;
    if (started == expected) {
        int64_t now = now_ns();
        if (now >= 0) {
            int64_t start_ns = now + warmup * NS_PER_SECOND;
            end_ns = start_ns + window.duration_ns;
            atomic_store(&window.start_ns, start_ns);
            printf("Aquecimento: %ld s. Janela de medicao: %ld s.\n",
                   warmup, duration);
            fflush(stdout);
        }
    }

    unsigned long long total_bytes = 0;
    unsigned long long window_bytes = 0;
    int failed = end_ns == 0;

    // O join garante que a thread terminou de escrever seu contador.
    for (int i = 0; i < started; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            fprintf(stderr, "Erro ao aguardar thread.\n");
            return EXIT_FAILURE;
        }

        if (clients[i].failed) {
            failed = 1;
            continue;
        }

        if (end_ns != 0 && clients[i].eof_ns < end_ns) {
            fprintf(stderr, "Conexao %d encerrou antes do fim da janela.\n",
                    i + 1);
            failed = 1;
        }

        printf("Conexao %d: %llu bytes recebidos, %llu na janela\n",
               i + 1, clients[i].total_bytes, clients[i].window_bytes);
        total_bytes += clients[i].total_bytes;
        window_bytes += clients[i].window_bytes;
    }

    if (failed)
        return EXIT_FAILURE;

    printf("Total agregado: %llu bytes recebidos\n", total_bytes);
    printf("Total na janela: %llu bytes recebidos\n", window_bytes);
    double mbps = (double)window_bytes * 8.0 / ((double)duration * 1000000.0);
    printf("Vazao agregada na janela: %.6f Mbps\n", mbps);

    for (long second = 0; second < duration; second++) {
        unsigned long long bytes = 0;
        for (int i = 0; i < started; i++)
            bytes += clients[i].bytes_per_second[second];

        printf("Segundo %ld: %llu bytes, %.6f Mbps\n",
               second + 1, bytes, (double)bytes * 8.0 / 1000000.0);
    }

    return EXIT_SUCCESS;
}
