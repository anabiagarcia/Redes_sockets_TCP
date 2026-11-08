#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/in.h>


#define BLOCK_DEFAULT (64u * 1024u) //Bloco padrao de 64 KiB.
#define BLOCK_MIN     1L
#define BLOCK_MAX     (16L * 1024L * 1024L) 
#define DURATION_MIN  0.001 
#define DURATION_MAX  3600.0
#define DRAIN_TIMEOUT_S 5 //Tempo limite para o servidor fechar a conexao depois que avisamos que nao ha mais dados
#define SEND_TIMEOUT_S 5 //Tempo limite de um send() individual

typedef struct {
    const char *ip;        
    int         port;      
    double      duration;  
    size_t      block;     //bytes enviados por chamada send()
    int         id;        
} Config;

//Medicao de tempo: Devolve o instante atual, em segundos
double now_seconds(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

// Fecha a conexao de forma que nenhum byte enviado se perca.
void finish_connection(int fd, int id) {
    char sink[4096];
    struct timeval timeout;

    timeout.tv_sec  = DRAIN_TIMEOUT_S;
    timeout.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    //ENOTCONN significa que a conexao ja caiu
    if (shutdown(fd, SHUT_WR) == -1 && errno != ENOTCONN)
        return;

    while (1) {
        ssize_t n = recv(fd, sink, sizeof(sink), 0);

        if (n > 0)
            continue;        

        if (n == 0)
            break;//EOF: fechou

        if (errno == EINTR)
            continue;

        // O servidor não retornou o fim e o tempo de espera acabou
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            fprintf(stderr, "[cliente %d] AVISO: servidor nao confirmou o "
                            "fim em %d s; total pode divergir.\n",
                    id, DRAIN_TIMEOUT_S);
            break;
        }

        break;             
    }
}

/* Aloca o bloco que sera transmitido e preenche UMA unica vez
 * Gerar ou copiar o conteudo a cada iteracao mediria a velocidade de escrever
 * em memoria, nao a de transmitir pela rede.*/
char *create_payload(size_t size) {
    char *block = malloc(size);
    size_t i;

    if (block == NULL)
        return NULL;

    for (i = 0; i < size; i++)
        block[i] = (char)('A' + (i % 26));

    return block;
}

//Imprime o resultado da medicao em dois lugares, uma linha CSV e um resumo para quando o cliente roda a mao                                            
void report(const Config *cfg, unsigned long long bytes, unsigned long long calls, double elapsed) {
    
    double mbps = 0.0;

    if (elapsed > 0.0)
        mbps = (double)bytes * 8.0 / elapsed / 1e6;

    //csv
    printf("CLIENT,%d,%llu,%.6f,%.4f,%llu\n",
           cfg->id, bytes, elapsed, mbps, calls);
    fflush(stdout);

    /resumo na tela
    fprintf(stderr,
            "[cliente %d] enviados %llu bytes (%.2f MiB) em %.3f s\n"
            "[cliente %d] taxa local: %.2f Mbps  |  %llu chamadas send()"
            ", media de %.0f bytes por chamada\n",
            cfg->id, bytes, (double)bytes / 1048576.0, elapsed,
            cfg->id, mbps, calls,
            calls > 0 ? (double)bytes / (double)calls : 0.0);
}

/* Converte texto em inteiro, exigindo que o texto seja um numero
 * completo e que o valor caia na faixa [min, max].
 * Retorna 0 em sucesso e -1 em erro.                                 */
int parse_long(const char *text, long min, long max, long *out) {
    char *end = NULL;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);

    //end == text: nao havia digito algum no inicio.
    //end != '\0': sobrou lixo depois do numero.
    if (end == text || *end != '\0')
        return -1;

    //ERANGE: o numero nao cabe em um long.
    if (errno == ERANGE)
        return -1;

    if (value < min || value > max)
        return -1;

    *out = value;
    return 0;
}

//Mesma ideia da funcao acima, para valores fracionarios.
int parse_double(const char *text, double min, double max, double *out) {
    char *end = NULL;
    double value;

    errno = 0;
    value = strtod(text, &end);

    if (end == text || *end != '\0')
        return -1;

    if (errno == ERANGE)
        return -1;

    if (!(value >= min && value <= max))
        return -1;

    *out = value;
    return 0;
}

void usage(const char *prog) {
    fprintf(stderr,
        "Uso: %s <IP> <porta> <duracao_s> [tamanho_bloco] [id]\n"
        "\n"
        "  IP             endereco IPv4 do servidor (ex.: 127.0.0.1)\n"
        "  porta          porta TCP do servidor (1..65535)\n"
        "  duracao_s      tempo de transmissao em segundos (ex.: 30)\n"
        "  tamanho_bloco  bytes por send() (padrao: %u, maximo: %ld)\n"
        "  id             identificador deste cliente no CSV (padrao: 0)\n"
        "\n"
        "Exemplo: %s 127.0.0.1 8080 30 65536 1\n",
        prog, (unsigned)BLOCK_DEFAULT, BLOCK_MAX, prog);
}

//Preenche cfg a partir de argv
int parse_args(int argc, char *argv[], Config *cfg) {
    long value;

    if (argc < 4 || argc > 6) {
        usage(argv[0]);
        return -1;
    }

    cfg->ip = argv[1];

    if (parse_long(argv[2], 1, 65535, &value) != 0) {
        fprintf(stderr, "Erro: porta invalida ('%s'). "
                        "Informe um inteiro entre 1 e 65535.\n", argv[2]);
        return -1;
    }
    cfg->port = (int)value;

    if (parse_double(argv[3], DURATION_MIN, DURATION_MAX,
                     &cfg->duration) != 0) {
        fprintf(stderr, "Erro: duracao invalida ('%s'). "
                        "Informe um valor entre %.3f e %.0f segundos.\n",
                argv[3], DURATION_MIN, DURATION_MAX);
        return -1;
    }

    /* Os dois ultimos argumentos sao opcionais. */
    cfg->block = BLOCK_DEFAULT;

    if (argc >= 5) {
        if (parse_long(argv[4], BLOCK_MIN, BLOCK_MAX, &value) != 0) {
            fprintf(stderr, "Erro: tamanho de bloco invalido ('%s'). "
                            "Informe um inteiro entre %ld e %ld bytes.\n",
                    argv[4], BLOCK_MIN, BLOCK_MAX);
            return -1;
        }
        cfg->block = (size_t)value;
    }

    cfg->id = 0;

    if (argc >= 6) {
        if (parse_long(argv[5], 0, 9999, &value) != 0) {
            fprintf(stderr, "Erro: id invalido ('%s'). "
                            "Informe um inteiro entre 0 e 9999.\n", argv[5]);
            return -1;
        }
        cfg->id = (int)value;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    struct sockaddr_in server_addr;
    Config cfg;
    int sockfd;
    char *block;
    double t_start, t_end, deadline;

    //Contador de 64 bits
    unsigned long long total_bytes = 0;
    unsigned long long send_calls  = 0;
    int send_failed = 0;

    //Chamar o tradutor e abortar se der errado
    if (parse_args(argc, argv, &cfg) != 0)
        return EXIT_FAILURE;

    // imprimi na tela os parâmetros 
    fprintf(stderr,
            "[cliente %d] servidor=%s:%d duracao=%.3fs bloco=%zu bytes\n",
            cfg.id, cfg.ip, cfg.port, cfg.duration, cfg.block);

    signal(SIGPIPE, SIG_IGN);

    //criar socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    //monta endereço
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(cfg.port);

    //traduzir e colocar o IP
    if (inet_pton(AF_INET, cfg.ip,
                  &server_addr.sin_addr) != 1) {
        fprintf(stderr, "Erro: endereco IPv4 invalido ('%s').\n", cfg.ip);
        close(sockfd);
        return EXIT_FAILURE;
    }

    //conectar
    if (connect(sockfd, (struct sockaddr *)&server_addr,
                sizeof(server_addr)) == -1) {
        perror("connect");
        close(sockfd);
        return EXIT_FAILURE;
    }

    //Consulta (sem alterar) o tamanho do buffer de envio do socket
    {
        int sndbuf = 0;
        socklen_t optlen = sizeof(sndbuf);

        if (getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF,
                       &sndbuf, &optlen) == 0)
            fprintf(stderr, "[cliente %d] SO_SNDBUF = %d bytes\n",
                    cfg.id, sndbuf);
    }

    //cronômetro de segurança em cada send()
    {
        struct timeval send_timeout;

        send_timeout.tv_sec  = SEND_TIMEOUT_S;
        send_timeout.tv_usec = 0;

        setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO,
                   &send_timeout, sizeof(send_timeout));
    }

    /* ---------------------------------------------------------------- *
     * Transmissao cronometrada
     * ---------------------------------------------------------------- */

    block = create_payload(cfg.block);

    if (block == NULL) {
        fprintf(stderr, "Erro: memoria insuficiente para um bloco "
                        "de %zu bytes.\n", cfg.block);
        close(sockfd);
        return EXIT_FAILURE;
    }

    t_start  = now_seconds();
    deadline = t_start + cfg.duration;

    // Laco principal da medicao
    while (now_seconds() < deadline) {
        ssize_t n = send(sockfd, block, cfg.block, MSG_NOSIGNAL);

        if (n == -1) {
            //um sinal chegou durante a chamada
            if (errno == EINTR)
                continue;

            //o servidor encerrou
            if (errno == EPIPE || errno == ECONNRESET) {
                fprintf(stderr, "[cliente %d] servidor encerrou a "
                                "conexao antes do prazo.\n", cfg.id);
                break;
            }

            //o servidor ficou tempo demais sem liberar espaco no buffer           
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                fprintf(stderr, "[cliente %d] aviso: send() bloqueado "
                                "por %d s sem progresso.\n",
                        cfg.id, SEND_TIMEOUT_S);
                continue;
            }

            perror("send");
            send_failed = 1;
            break;
        }

        if (n == 0)
            break;

        //send() pode aceitar menos bytes do que foi pedido
        total_bytes += (unsigned long long)n;
        send_calls++;
    }

    /* Marcado imediatamente apos o laco, antes de qualquer encerramento,
     * para que o intervalo cubra so a transmissao.                    */
    t_end = now_seconds();

    free(block);

    /* Ordem importa: avisa o servidor e espera ele fechar ANTES de
     * liberar o descritor.                                           */
    finish_connection(sockfd, cfg.id);
    close(sockfd);

    report(&cfg, total_bytes, send_calls, t_end - t_start);

    return send_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
