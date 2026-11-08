# Redes_sockets_TCP

## Servidor de medição TCP

O `throughput_server.c` é o servidor de medição. O servidor de chat original
continua em `chat_server.c`.

- O servidor aceita de 1 a 10 conexões por rodada e cria uma thread para cada
  cliente. Cada thread usa um buffer de 65.536 bytes em `recv()`, soma a
  quantidade efetivamente lida em contadores de 64 bits e descarta o conteúdo.
- A recepção começa assim que cada cliente conecta. Quando todas as conexões
  esperadas são aceitas, o servidor programa o aquecimento e uma janela comum
  de medição com `CLOCK_MONOTONIC`. O instante em que `recv()` termina
  determina se seus bytes entram na janela.
- Depois da janela, as threads continuam recebendo até o EOF. A thread
  principal aguarda todas terminarem com `pthread_join()` e apresenta os totais
  por conexão e os totais agregados, incluindo quantos bytes foram lidos
  dentro da janela. Um cliente que encerra antes do fim da janela invalida a
  rodada, que termina com código de erro.

### Compilar e testar no Linux ou WSL

Na pasta do repositório, compile o servidor e o cliente de medição:

```bash
cc -std=c11 -D_POSIX_C_SOURCE=200809L -O2 -Wall -Wextra -pthread throughput_server.c -o throughput_server
cc -std=c11 -D_POSIX_C_SOURCE=200809L -O2 -Wall -Wextra chat_client.c -o throughput_client
```

No primeiro terminal, execute `./throughput_server <porta> <num_clientes> <aquecimento_s> <janela_s>`.
O aquecimento e a janela aceitam de 1 a 3.600 segundos inteiros. Para testar
com dois clientes, um segundo de aquecimento e dois segundos de janela:

```bash
./throughput_server 8080 2 1 2
```

No segundo terminal, inicie os dois clientes em paralelo:

```bash
./throughput_client 127.0.0.1 8080 5 65536 1 &
./throughput_client 127.0.0.1 8080 5 65536 2 &
wait
```

Cada cliente começa a cronometrar seu envio individualmente. Use uma duração
que cubra a diferença entre as partidas, o aquecimento e toda a janela.
Na saída do servidor, `Total na janela` é a soma dos bytes que entram na
medição. Em rodadas válidas, `Vazao agregada na janela` é calculada em Mbps
por `bytes_na_janela × 8 / (janela_s × 1.000.000)`.

Ao fim de uma rodada válida, o servidor também imprime uma linha `Segundo N`
para cada segundo da janela, começando em 1. Cada linha traz a soma dos bytes
lidos por todas as threads naquele intervalo e sua vazão em Mbps. Segundos
sem leituras aparecem com zero. A soma dos bytes das amostras deve coincidir
com `Total na janela`.
