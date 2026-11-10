# Redes_sockets_TCP

Este projeto mede o quanto de dados um cliente consegue, de fato, mandar para um
servidor usando TCP. A ideia é rodar de 1 a 10 clientes ao mesmo tempo e ver
qual a vazão agregada que chega no servidor em cada cenário, em Mbps.

O ponto de partida foi uma aplicação de chat TCP, que adaptamos para transmitir
dados de forma contínua e automática, sem ninguém precisar digitar nada. Assim
dá para medir a vazão de um jeito confiável.

## O que tem em cada arquivo

- **`chat_server.c`** — é o servidor que faz a medição. Ele aceita de 1 a 10
  conexões, recebe os dados de cada cliente numa thread separada, conta os bytes
  e joga o conteúdo fora. É daqui que saem os números do experimento.
- **`chat_client.c`** — é o cliente que gera o tráfego. Ele transmite dados por
  um tempo definido e, no fim, informa quanto enviou.

Vale notar: o cliente só envia dados. Ele chega a ler algo do servidor em um
único momento, bem no fim, só para ter certeza de que o servidor recebeu tudo
antes de fechar a conexão. Não tem troca de mensagem no meio da medição.

## Antes de começar

Você vai precisar de:

- um compilador C com suporte a C11 (`cc`, `gcc` ou `clang`);
- a biblioteca `pthread`;
- um ambiente POSIX (Linux, WSL ou macOS).

Os comandos abaixo rodam a partir da raiz do repositório.

## Compilando

```bash
cc -std=c11 -D_POSIX_C_SOURCE=200809L -O2 -Wall -Wextra -pthread chat_server.c -o chat_server
cc -std=c11 -D_POSIX_C_SOURCE=200809L -O2 -Wall -Wextra chat_client.c -o chat_client
```

## Como a medição acontece

O servidor abre uma thread para cada cliente que conecta. Cada thread fica
chamando `recv()` com um buffer de 64 KB, soma o que leu em contadores de 64
bits e descarta o conteúdo.

A recepção já começa assim que cada cliente conecta. Quando todas as conexões
esperadas chegam, o servidor marca um tempo de aquecimento e, logo depois, uma
janela de medição comum a todos, usando `CLOCK_MONOTONIC`. O que define se um
bloco entra ou não na conta é o instante em que o `recv()` daquele bloco termina.

Passada a janela, as threads continuam recebendo até o cliente encerrar (EOF).
A thread principal espera todas terminarem com `pthread_join()` e aí mostra os
totais por conexão e o total agregado, incluindo quantos bytes caíram dentro da
janela. Se algum cliente fechar antes do fim da janela, a rodada é considerada
inválida e termina com erro.

A vazão agregada da janela sai em Mbps pela conta
`bytes_na_janela × 8 / (janela_s × 1.000.000)`, somando os bytes recebidos por
todas as threads durante aquela janela comum.

## Rodando

No primeiro terminal, suba o servidor:

```bash
./chat_server <porta> <num_clientes> <aquecimento_s> <janela_s>
```

Os argumentos são:

- `porta` — porta TCP de escuta (1 a 65535);
- `num_clientes` — quantas conexões a rodada espera (1 a 10);
- `aquecimento_s` — segundos de aquecimento antes de medir (1 a 3600);
- `janela_s` — quanto tempo a janela de medição dura (1 a 3600).

Por exemplo, com dois clientes, um segundo de aquecimento e dois de janela:

```bash
./chat_server 8080 2 1 2
```

No segundo terminal, suba os clientes em paralelo. O cliente segue a forma
`./chat_client <IP> <porta> <duracao_s> [tamanho_bloco] [id]`:

```bash
./chat_client 127.0.0.1 8080 5 65536 1 &
./chat_client 127.0.0.1 8080 5 65536 2 &
wait
```

Onde:

- `IP` e `porta` apontam para o servidor;
- `duracao_s` é por quanto tempo o cliente transmite. Escolha um valor que cubra
  a diferença entre as partidas dos clientes, o aquecimento e a janela inteira;
- `tamanho_bloco` são os bytes enviados por `send()` (o padrão é 65.536);
- `id` é só um identificador do cliente na saída.

Para fazer os cenários de 1 a 10 clientes, é só repetir isso ajustando o
`num_clientes` no servidor e quantos clientes você sobe em paralelo.

## Entendendo a saída

- `Total na janela` é a soma dos bytes que entraram na medição.
- `Vazao agregada na janela` é o resultado que interessa, em Mbps.
- As linhas `Segundo N` mostram, para cada segundo da janela, quantos bytes
  todas as threads leram naquele intervalo e a vazão correspondente. Segundos
  sem leitura aparecem com zero, e a soma de todas as amostras tem que bater com
  o `Total na janela`.
