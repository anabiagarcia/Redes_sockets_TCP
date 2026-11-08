# Decisões de projeto do cliente de medição

Referência das escolhas feitas em `chat_client.c`. A ordem das seções
acompanha a ordem do código, para que ao remover um comentário você
encontre aqui a justificativa correspondente.

Dados de ambiente usados ao longo do texto: Apple clang 17.0.0,
`arm64-apple-darwin24.6.0`, `net.inet.tcp.sendspace` = 131072 com ajuste
automático até 4 MB, MTU do `lo0` = 16384.

---

## Índice rápido

| Elemento no código | Seção |
|---|---|
| Cabeçalho do arquivo, cliente como conferência | [1](#1-o-número-do-cliente-é-conferência-não-resultado) |
| Programa monothread | [2](#2-por-que-o-cliente-é-monothread) |
| `#include <sys/time.h>` | [3](#3-includes-não-óbvios) |
| `BLOCK_DEFAULT`, `BLOCK_MIN`, `BLOCK_MAX` | [4](#4-tamanho-do-bloco-64-kib) |
| `DURATION_MIN`, `DURATION_MAX` | [5](#5-limites-de-duração) |
| `DRAIN_TIMEOUT_S` | [11](#11-finish_connection-encerramento-ordenado) |
| `SEND_TIMEOUT_S`, `SO_SNDTIMEO` | [6](#6-so_sndtimeo-rede-de-segurança) |
| `struct Config` | [7](#7-agrupar-os-parâmetros-em-uma-struct) |
| `now_seconds()`, `CLOCK_MONOTONIC` | [8](#8-escolha-do-relógio) |
| `signal(SIGPIPE, SIG_IGN)` | [9](#9-sigpipe-ignorado-obrigatório) |
| Ausência de tratamento de `SIGINT` | [10](#10-por-que-não-há-tratamento-de-sigint) |
| `finish_connection()`, `shutdown(SHUT_WR)` | [11](#11-finish_connection-encerramento-ordenado) |
| `create_payload()` | [12](#12-geração-do-bloco-de-dados) |
| `report()`, CSV, fórmula do Mbps | [13](#13-saída-do-resultado) |
| `parse_long()`, `parse_double()` | [14](#14-leitura-dos-argumentos) |
| Contadores `unsigned long long` | [15](#15-contadores-de-64-bits) |
| `getsockopt(SO_SNDBUF)` | [16](#16-consultar-sem-alterar-o-so_sndbuf) |
| Laço de envio e tratamento de erros | [17](#17-o-laço-de-envio) |
| Posição de `t_end` | [18](#18-onde-o-cronômetro-para) |
| Códigos de saída | [19](#19-códigos-de-saída) |
| O que foi deliberadamente omitido | [20](#20-o-que-não-está-no-código-e-por-quê) |

---

## 1. O número do cliente é conferência, não resultado

`send()` retorna quando o núcleo do sistema copia os dados para o buffer
de envio do socket, **não** quando o servidor os recebe. No instante em
que o prazo vence existem bytes já contados pelo cliente que ainda estão
em trânsito ou dentro do buffer do kernel.

Logo, o valor oficial do relatório tem de vir do servidor, que conta o
que efetivamente leu. O número do cliente serve para conferir se os dois
lados concordam — e, se divergirem muito, sinalizar que algo está errado.

Com a sequência de encerramento da seção 11, a divergência observada nos
testes foi **zero**: os totais bateram exatamente em todas as execuções.
Sem aquela sequência, o cliente contaria mais que o servidor.

## 2. Por que o cliente é monothread

O chat original tinha duas linhas de execução porque precisava esperar o
teclado e exibir mensagens que chegavam, ao mesmo tempo. Nenhuma das duas
necessidades existe aqui: a atividade mede só o sentido cliente →
servidor, e o cliente não recebe nada durante o teste.

Remover a thread não é só simplificação estética. Ela introduziria custo
de sincronização e disputa por processador dentro do próprio processo que
está sendo medido — uma variável de confusão na medida. Com uma única
thread, o que o cliente mede é transmissão, e nada mais.

Consequência prática: o cliente não precisa de `-pthread` na compilação.

## 3. Includes não óbvios

| Cabeçalho | Para quê |
|---|---|
| `<time.h>` | `clock_gettime`, `struct timespec`, `CLOCK_MONOTONIC` |
| `<sys/time.h>` | `struct timeval`, exigida por `SO_RCVTIMEO` e `SO_SNDTIMEO` |
| `<errno.h>` | `errno`, `EINTR`, `EPIPE`, `ECONNRESET`, `EAGAIN`, `ERANGE` |
| `<signal.h>` | `signal`, `SIG_IGN`, `SIGPIPE` |

`<pthread.h>` foi removido junto com a thread receptora.

## 4. Tamanho do bloco: 64 KiB

Cada `send()` custa uma chamada de sistema, com entrada e saída do núcleo
do sistema operacional. **Esse custo é pago por chamada, não por byte.**
Quanto mais bytes cada chamada carregar, menor o peso das chamadas de
sistema na medida.

Medido nesta máquina, em loopback, 2 segundos por ponto:

| Bloco | Mbps | Chamadas `send()` |
|---|---|---|
| 1 B | 8,28 | 2.070.698 |
| 64 B | 503 | 1.966.640 |
| 1 KiB | 6.581 | 1.606.795 |
| 16 KiB | 36.201 | 552.389 |
| **64 KiB** | **75.487** | **287.961** |
| 256 KiB | 91.054 | 86.836 |

A linha de 1 byte é a razão de o `recv` byte a byte do código original
ter sido descartado: 8,28 Mbps é o teto daquela abordagem, qualquer que
seja a rede. Esses números medem cópia de memória e syscalls, não uma
rede de verdade — são o teto do software, não do enlace.

O padrão ficou em 64 KiB por ser o valor usual em testes de vazão e por
já estar na região de retorno decrescente. Os 20% extras do bloco de
256 KiB aparecem em loopback, mas devem desaparecer em rede Gigabit,
onde o gargalo passa a ser o cabo. O tamanho é parâmetro de linha de
comando justamente para que a tabela acima possa ser reproduzida como
experimento secundário do relatório.

`BLOCK_MAX` é 16 MiB: um limite arbitrário mas deliberado, para que um
erro de digitação no script não faça o cliente tentar alocar gigabytes.

## 5. Limites de duração

`DURATION_MIN` 0,001 s e `DURATION_MAX` 3600 s. O mínimo evita divisão
por um intervalo tão curto que o resultado perderia sentido; o máximo
protege contra um zero extra digitado por engano, que deixaria o
experimento rodando por dias.

## 6. `SO_SNDTIMEO`: rede de segurança

Em operação normal esse limite nunca é alcançado. Ele existe para um caso
específico: se o servidor parar de ler, o buffer de envio enche, o
`send()` bloqueia indefinidamente e o cliente ultrapassaria o próprio
prazo sem conseguir sair. Numa bateria automatizada de 10 cenários com
repetições, isso travaria o script inteiro.

Com o limite de 5 s, o `send()` devolve `EAGAIN`, o laço volta ao topo,
reavalia o prazo e encerra normalmente. A medição não é afetada, porque
o tempo limite só dispara quando já não há progresso algum.

## 7. Agrupar os parâmetros em uma `struct`

`Config` reúne IP, porta, duração, bloco e id. A alternativa seria
espalhar acessos a `argv[]` pelo corpo do programa, ou passar cinco
argumentos soltos para cada função.

O ganho concreto: `report()` recebe um `const Config *` e tem acesso ao
que precisa sem que a assinatura cresça a cada parâmetro novo.

## 8. Escolha do relógio

`CLOCK_MONOTONIC` conta a partir de um ponto arbitrário no passado e
**nunca retrocede**. Essa propriedade é o motivo da escolha:

- `time()` tem resolução de 1 segundo, grosseira demais para um
  intervalo que precisa ser conhecido com precisão de milissegundos.
- `gettimeofday()` e `CLOCK_REALTIME` seguem o relógio de parede. Se o
  sistema sincronizar a hora pela rede no meio do teste, o relógio dá um
  salto, e a medição é corrompida sem deixar rastro. Um salto para trás
  pode até produzir um intervalo negativo.

O retorno é `double` em segundos. A precisão é suficiente: um `double`
tem 53 bits de mantissa, o que mantém resolução melhor que microssegundo
para valores na casa dos milhões de segundos.

Ler o relógio a cada volta do laço é aceitável porque, nesta plataforma,
`clock_gettime(CLOCK_MONOTONIC)` é resolvido sem troca de modo de
execução — dezenas de nanossegundos, contra um `send()` de 64 KiB que
custa ordens de magnitude mais. Com blocos muito pequenos a proporção
muda, mas aí o experimento já não é o principal.

## 9. `SIGPIPE` ignorado (obrigatório)

```c
signal(SIGPIPE, SIG_IGN);
```

Por padrão no Unix, escrever em uma conexão que o outro lado já fechou
**termina o processo imediatamente**, sem mensagem alguma, com código de
saída 141 (128 + 13).

Esse cenário não é hipotético no experimento. O servidor encerra a janela
de medição e fecha as conexões; qualquer cliente que ainda esteja dentro
do próprio prazo vai chamar `send()` numa conexão morta. Sem o
`SIG_IGN`, aquele cliente morreria antes de imprimir sua linha CSV, e o
script registraria uma repetição perdida sem nenhuma explicação.

Com o sinal ignorado, `send()` devolve `-1` com `errno == EPIPE`, o laço
trata, o cliente registra "servidor encerrou a conexão antes do prazo",
reporta o que conseguiu transmitir e sai com código 0 — um dado
utilizável em vez de um buraco nos resultados.

Esta é a única linha de tratamento de sinal no cliente, e ela não tem
relação com interatividade.

**Verificado:** com um servidor que fecha a conexão em 1 s enquanto o
cliente pede 10 s, o cliente sobreviveu, imprimiu o CSV e saiu com
código 0.

Nesse teste os totais divergiram: 8.670.347.264 no cliente contra
8.668.774.400 no servidor, uma diferença de 1.572.864 bytes. A
divergência é esperada e ilustra a seção 1 — o servidor fechou de forma
abrupta, e os bytes que estavam nos buffers e em trânsito se perderam.
No encerramento ordenado (seção 11) os dois lados batem exatamente.

## 10. Por que não há tratamento de `SIGINT`

Uma versão anterior deste cliente tratava `SIGINT` com `sigaction`, para
que o Ctrl-C produzisse um resultado parcial e um encerramento limpo em
vez de matar o processo. Foi removido deliberadamente.

**Razão:** o procedimento do experimento é determinado de ponta a ponta.
O cliente recebe duração e tamanho de bloco na linha de comando, e para
sozinho quando o prazo vence. Interrupção manual não faz parte do
procedimento, e o Ctrl-C continua funcionando mesmo sem tratador — o
processo simplesmente termina, e o que se perde é apenas o resultado
parcial de uma repetição que seria descartada de todo modo.

Com a remoção saíram `stop_requested`, `handle_interrupt`,
`install_signal_handlers`, e a verificação da bandeira na condição do
laço, que agora é apenas `while (now_seconds() < deadline)`.

**Um detalhe que pesou na decisão:** se o script de experimento matar os
clientes — com `timeout`, um `trap` ou `kill` — o sinal padrão é
`SIGTERM`, não `SIGINT`. O tratador anterior não cobria `SIGTERM`, então
não oferecia a robustez que aparentava oferecer. Se no futuro o script
passar a depender de matar processos, o caminho é tratar `SIGTERM`, e não
reintroduzir o `SIGINT`.

## 11. `finish_connection()`: encerramento ordenado

Três passos, nesta ordem:

1. `shutdown(fd, SHUT_WR)` envia o FIN do TCP. Comunica ao servidor "não
   vem mais dado meu", mantendo a direção de leitura aberta.
2. Lê até `recv()` devolver 0, o sinal de que o servidor de fato fechou.
3. `close(fd)`.

**Por que não chamar `close()` direto.** Com dados ainda em trânsito, um
`close()` pode cortar a conexão de forma abrupta, com um pacote RST, e
fazer o servidor descartar os últimos bytes. A medição sairia mais baixa
que a realidade, sem nenhum aviso. Foi exatamente esse passo que fez os
totais dos dois lados baterem byte a byte nos testes.

**`ENOTCONN` é tolerado** porque significa que a conexão já caiu —
situação normal se o servidor encerrou antes, e não um erro a reportar.

**`DRAIN_TIMEOUT_S` (via `SO_RCVTIMEO`)** evita que um servidor que nunca
fecha deixe o cliente esperando para sempre. As duas peças funcionam em
par: sem o `setsockopt`, o `recv` bloquearia indefinidamente e o ramo
`EAGAIN` nunca seria alcançado.

Quando o limite estoura, o cliente **avisa e segue**, em vez de abortar.
A razão: nesse ponto a transmissão já terminou e o cronômetro já parou
(seção 18), então os números são válidos. O que se perde é apenas a
confirmação de que o servidor drenou tudo — motivo para desconfiar da
repetição, não para descartar a medição.

A mensagem é uma linha só, e diz a consequência em vez de apenas o fato:

```
[cliente 3] AVISO: servidor nao confirmou o fim em 5 s; total pode divergir.
```

O prefixo `AVISO` permite ao script localizá-la com `grep AVISO`. Vai
para `stderr`, nunca para `stdout`, para não corromper a linha CSV.

**Verificado** com um servidor que lê tudo e então fica parado sem
fechar: o cliente transmitiu 2 s, esperou os 5 s do limite, emitiu o
aviso, imprimiu o CSV normalmente e saiu com código 0. Em operação
normal esse aviso nunca aparece.

O buffer `sink` de 4 KiB existe só para descartar o que o servidor
eventualmente envie. Nada ali é contabilizado: o experimento mede apenas
o sentido de subida.

## 12. Geração do bloco de dados

O bloco é alocado e preenchido **uma única vez**, antes do laço. Essa é a
regra que mais importa nesta função: gerar ou copiar o conteúdo a cada
iteração mediria a velocidade de escrever em memória, não a de
transmitir pela rede.

O conteúdo não tem significado — são bytes de enchimento, porque o
experimento mede volume transportado, não informação. O padrão usa letras
(`'A' + i % 26`) em vez de zeros por precaução, para que nenhuma camada
do caminho possa tratar o bloco como vazio.

## 13. Saída do resultado

**Duas vias, com propósitos distintos.** `stdout` recebe uma linha CSV;
`stderr` recebe o resumo legível. A separação permite que o script de
experimento faça `>> resultados.csv` sem capturar texto informativo no
meio dos dados.

**Colunas:** `CLIENT, id, bytes, segundos, mbps, chamadas_send`

**Sem cabeçalho.** Com 10 clientes rodando, apareceriam 10 cabeçalhos
espalhados pelos dados. Quem cria o arquivo e escreve o cabeçalho é o
script.

**A fórmula:**

```
mbps = bytes * 8.0 / elapsed / 1e6
```

- `* 8` converte bytes em bits; a unidade pedida é megabits.
- `/ 1e6`, não `/ 1048576`. Em taxas de transmissão "mega" vale 10⁶
  exatos. Usar 2²⁰ produziria um número 4,9% menor que o correto.
- `elapsed` é o tempo **medido**, não a duração pedida na linha de
  comando. As duas diferem um pouco, porque o último `send()` do laço
  começa antes do prazo vencer e termina depois.

A guarda `if (elapsed > 0.0)` evita divisão por zero em execuções
degeneradas.

**A razão `bytes / chamadas`** aparece no resumo como diagnóstico: se
ficar abaixo do tamanho do bloco, houve envios parciais, o que indica
pressão no buffer de envio. Nos testes ficou exatamente em 65536, ou
seja, nenhum envio parcial.

## 14. Leitura dos argumentos

**`strtol`/`strtod` em vez de `atoi`/`atof`.** `atoi` não sinaliza erro:
diante de um texto inválido devolve `0` silenciosamente. Em um
experimento isso é perigoso — um argumento digitado errado no script
faria a bateria inteira rodar com duração zero, e você só descobriria ao
olhar resultados vazios. Ou não descobriria.

As funções detectam três erros que o `atoi` ignora:

| Verificação | Captura |
|---|---|
| `end == text` | nenhum dígito no início (`"abc"`) |
| `*end != '\0'` | lixo sobrando depois do número (`"30s"`) |
| `errno == ERANGE` | valor que não cabe no tipo |

**Rejeição de NaN.** Em `parse_double` a comparação final está escrita
como negação:

```c
if (!(value >= min && value <= max))
    return -1;
```

Qualquer comparação com `NaN` é falsa, inclusive `NaN >= min`. Escrita
assim, a condição rejeita `NaN` junto. Na forma direta
(`if (value < min || value > max)`), um `NaN` passaria.

**Argumentos posicionais, os dois últimos opcionais.** Preferido a flags
(`--bloco=N`) por ser bem menos código para a mesma função, num programa
que recebe no máximo cinco valores.

**Mensagens de erro nomeiam o argumento recusado e a faixa esperada.**
Durante uma bateria de experimentos, "porta inválida (`'8080abc'`)" é
diretamente acionável; "argumento inválido" não é.

## 15. Contadores de 64 bits

```c
unsigned long long total_bytes = 0;
```

Um inteiro de 32 bits estoura em 4,29 GB. Em rede Gigabit isso são menos
de 40 segundos de teste; em loopback, menos de meio segundo. No estouro o
contador volta a zero e o resultado vira lixo **sem nenhum aviso** — o
programa continua rodando e imprime um número plausível mas errado.

Nos testes de 3 segundos em loopback o contador chegou a 27,4 bilhões de
bytes, mais de seis vezes o limite de 32 bits. A escolha não é teórica.

## 16. Consultar, sem alterar, o `SO_SNDBUF`

O tamanho do buffer de envio limita quantos bytes podem estar em trânsito
e, portanto, influencia a vazão alcançável. O relatório técnico precisa
declarar esse valor.

O código **consulta** com `getsockopt` e exibe, mas não altera. O motivo:
fixar o valor com `setsockopt` desativaria o ajuste automático do núcleo,
que nesta máquina cresce até 4 MB conforme a necessidade. Medir com o
padrão do sistema é mais representativo, e o valor fica registrado.

Observado nos testes: **146988 bytes**, acima do `sendspace` de 131072
informado pelo `sysctl`, porque o valor reportado inclui a margem que o
núcleo reserva para estruturas de controle.

## 17. O laço de envio

Quatro regras que o laço respeita, e que valem mais que sua aparência
simples:

- **Nenhuma escrita em tela ou arquivo.** `printf` é mais lento que a
  rede e dominaria o resultado.
- **Nenhum `memset`/`memcpy`/`snprintf`.** O bloco já está pronto.
- **Nenhum `sleep`.** Queremos a velocidade máxima.
- **Nenhum `socket()`/`close()`.** A conexão é aberta antes do laço.

**Acumular o retorno real de `send()`, nunca `cfg.block`.** `send()` pode
aceitar menos bytes do que foi pedido quando o buffer de envio está quase
cheio. Somar o tamanho pedido em vez do aceito superestimaria a vazão.

**Por que não existe mais um `send_all()`.** No chat, um envio parcial
precisava de insistência, para não quebrar uma mensagem no meio. Aqui o
conteúdo é enchimento e não há mensagem a preservar: se o envio foi
parcial, basta contar o que saiu e seguir com um bloco inteiro na próxima
volta. Isso também evita um laço interno degenerado quando o buffer está
cheio.

**Tratamento de erros, por `errno`:**

| Condição | Ação | Razão |
|---|---|---|
| `EINTR` | `continue` | um sinal chegou durante a chamada; não é erro |
| `EPIPE`, `ECONNRESET` | registra e sai do laço | servidor encerrou; fim antecipado, não falha |
| `EAGAIN`, `EWOULDBLOCK` | avisa e `continue` | estouro do `SO_SNDTIMEO` (seção 6) |
| qualquer outro | `perror`, marca falha | erro real |
| retorno `0` | sai do laço | conexão encerrada |

O `continue` em `EINTR` e `EAGAIN` reavalia a condição do laço, então um
prazo já vencido encerra na mesma volta, sem gastar outra chamada.

## 18. Onde o cronômetro para

`t_end` é marcado **imediatamente** após o laço, antes de `free`,
`shutdown` ou `close`. O intervalo precisa cobrir apenas a transmissão.
Se a drenagem entrasse na conta, o tempo de encerramento inflaria o
denominador e a taxa calculada cairia.

## 19. Códigos de saída

`0` em sucesso, diferente de zero em falha real de envio. O script de
experimento precisa distinguir uma repetição válida de uma que falhou,
para descartar a segunda em vez de misturá-la à média.

O fechamento da conexão pelo servidor antes do prazo **não** é tratado
como falha: produz um resultado parcial legítimo, com código 0. Quem
decide se aquela repetição serve é o script, comparando a duração medida
com a pedida.

## 20. O que não está no código, e por quê

**`TCP_NODELAY`** — desativaria o algoritmo de Nagle, que atrasa o envio
de dados pequenos para agrupá-los. Com blocos de 64 KiB todos os
segmentos já saem cheios, então Nagle nunca entra em ação. Ligar a opção
não mudaria nada.

**`SO_LINGER`** — controla o comportamento de `close()` com dados
pendentes. A sequência da seção 11 já resolve o encerramento de forma
explícita e mais previsível.

**Partida sincronizada entre clientes** (`--start-at <epoch_ms>`) — dez
clientes disparados por um script sobem com dezenas de milissegundos de
diferença. A decisão foi absorver esse desalinhamento no servidor, que
medirá uma janela de regime permanente descartando o início. Se o
experimento passar a usar duas máquinas, vale reconsiderar.

**Verificação do conteúdo recebido** — o experimento mede volume, não
integridade. O TCP já garante que os bytes cheguem corretos e em ordem;
conferir o padrão consumiria processador sem agregar nada à medida.

---

## Como o cliente se chama

```
./cliente <IP> <porta> <duracao_s> [tamanho_bloco] [id]
```

| Argumento | Faixa | Padrão |
|---|---|---|
| `IP` | IPv4 válido | obrigatório |
| `porta` | 1–65535 | obrigatório |
| `duracao_s` | 0,001–3600 | obrigatório |
| `tamanho_bloco` | 1–16777216 | 65536 |
| `id` | 0–9999 | 0 |

Compilação: `cc -O2 -Wall -Wextra -o cliente chat_client.c`

Sem `-pthread`: o cliente é monothread (seção 2).

## Estado da verificação

Compila com zero avisos em `cc -O2 -Wall -Wextra` e em
`cc -std=c99 -O2 -Wall -Wextra -pedantic`.

Validado contra um servidor contador em seis tamanhos de bloco: em todas
as execuções com encerramento ordenado, o total contado pelo cliente
coincidiu **exatamente** com o total contado pelo servidor.

Validado também o caminho do `EPIPE` (seção 9), com um servidor que fecha
a conexão antes do prazo do cliente: o cliente sobrevive, reporta o
parcial e sai com código 0.
