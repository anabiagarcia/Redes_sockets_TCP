# Registro de testes — medição de vazão TCP

Documento de coleta de dados dos experimentos.

---

## 1. Ambiente de teste

### Máquina(s) utilizada(s)


| Item | Servidor (este Mac) | Cliente (outra máquina) |
|---|---|---|
| Modelo / fabricante | Apple Mac16,12 | Dell Latitude 7490 |
| Processador (modelo, núcleos, clock) | Apple M4, 10 núcleos | Intel Core i5-8350U @ 1.70 GHz, 4 núcleos / 8 threads |
| Memória RAM | 16 GB | 8 GB |
| Sistema operacional (nome e versão) | macOS 15.7.4 (build 24G517) | Windows + Ubuntu 26.04.1 LTS no WSL 2 |
| Compilador (nome e versão) | Apple clang 17.0.0 (clang-1700.4.4.1) | GCC 15.2 |

### Rede

| Item | Valor |
|---|---|
| Tipo de conexão | Wi-Fi (ambas as máquinas na mesma rede sem fio) |
| IP do servidor | 192.168.1.70 (interface en0) |
| Velocidade nominal do enlace | Desconhecida; será inferida pelo throughput medido |
| MTU (servidor en0) | 1500 |
| `SO_SNDBUF` observado (cliente) | 87040 bytes (padrão do Linux no WSL 2) |
| Buffers TCP do servidor (sendspace/recvspace) | 131072 / 131072 bytes |

> Nota: o cliente roda em WSL 2, que usa uma VM Linux com rede NAT por trás do
> Windows. Essa camada de virtualização/NAT adiciona uma sobrecarga que pode
> limitar a vazão e deve ser considerada como possível gargalo na análise.

---

## 2. Parâmetros dos experimentos

| Parâmetro | Valor | Observação |
|---|---|---|
| Tamanho do bloco (`tamanho_bloco`) | 65536 | bytes por `send()` (64 KiB) |
| Aquecimento (`aquecimento_s`) | 2 | s antes de medir |
| Janela de medição (`janela_s`) | 10 | s de medição |
| Duração do cliente (`duracao_s`) | 15 | cobre largada + aquecimento + janela + folga |
| Repetições por cenário | 3 | nº de rodadas por quantidade de clientes |
| Faixa de clientes | 1 a 10 | exigido pelo enunciado |

### Procedimento para comparabilidade

Mantido fixo entre todas as rodadas:

- Mesmas duas máquinas, mesma rede Wi-Fi, mesmo posicionamento físico.
- Mesmos parâmetros (bloco 64 KiB, aquecimento 2 s, janela 10 s, duração 15 s).
- Servidor sempre iniciado antes dos clientes, informando o nº de clientes
  esperado para aquela rodada.
- Os N clientes de cada cenário são lançados em paralelo pelo mesmo script,
  minimizando o atraso de largada entre eles.
- 3 repetições por cenário; usa-se a média. Rodadas inválidas (cliente que
  encerra antes do fim da janela, aviso de divergência, ou erro de envio) são
  descartadas e repetidas.
- Evitar outras cargas de rede/CPU nas máquinas durante os testes.

---

## 3. Como a vazão agregada é calculada

O servidor conta os bytes efetivamente lidos por todas as threads dentro da
janela de medição comum. A vazão agregada sai da linha
`Vazao agregada na janela`, calculada no servidor por:

```
throughput_Mbps = bytes_na_janela * 8 / (janela_s * 1.000.000)
```

Onde `bytes_na_janela` é a soma dos bytes recebidos por todas as conexões
durante a janela.

---
## 4. Resultados — clientes vs throughput agregado

| Nº clientes | Rodada 1 (Mbps) | Rodada 2 (Mbps) | Rodada 3 (Mbps) | Média (Mbps) | Observações |
|---|---|---|---|---|---|
| 1  | 225.72 | 232.19 | 231.17 | 229.69 | janela estável ~210–240 Mbps |
| 2  | 224.43 | 219.21 | 222.83 | 222.16 | reparte variável entre as conexões; total estável ~220 |
| 3  | 222.72 | 218.15 | 217.24 | 219.37 | reparte desigual entre as 3 conexões |
| 4  | 212.53 | 215.17 | 216.17 | 214.62 | leve queda vs 3 clientes |
| 5  | 210.48 | 215.20 | 212.28 | 212.65 | |
| 6  | 203.73 | 210.32 | 214.00 | 209.35 | maior dispersão entre rodadas |
| 7  | 204.96 | 203.02 | 206.30 | 204.76 | muito consistente entre rodadas |
| 8  | 204.26 | 201.37 | 205.17 | 203.60 | |
| 9  | 201.18 | 199.14 | 205.55 | 201.96 | aquecimento ajustado p/ 3s; 2 rodadas descartadas (ver §5) |
| 10 | 198.19 | 200.75 | 196.07 | 198.34 | aquecimento 3s |

---

## 5. Observações e saída bruta

Rodada descartada (cenário de 9 clientes), ilustrando o critério de
comparabilidade:

- **Descarte 1:** com aquecimento de 2 s, a janela abriu antes de todos os 9
  clientes começarem a transmitir. O "Segundo 1" registrou 0 bytes, puxando a
  média para 177.49 Mbps. Correção: aumentar o aquecimento para 3 s nesse
  cenário (o atraso de largada cresce com o nº de clientes).

Observação geral: em todos os cenários a vazão por conexão é repartida de forma
desigual entre os clientes, mas a soma (vazão agregada) se mantém estável — o
teto é imposto pelo meio Wi-Fi compartilhado, não pelos clientes individuais.

---

## 6. Análise (rascunho para o relatório)

Médias por cenário (3 rodadas válidas cada):

| Clientes | Média (Mbps) |
|---|---|
| 1  | 229.69 |
| 2  | 222.16 |
| 3  | 219.37 |
| 4  | 214.62 |
| 5  | 212.65 |
| 6  | 209.35 |
| 7  | 204.76 |
| 8  | 203.60 |
| 9  | 201.96 |
| 10 | 198.34 |

- **Impacto do aumento de clientes na vazão:** a vazão agregada permanece
  aproximadamente constante na faixa de ~200–230 Mbps, com uma leve queda
  monotônica conforme o nº de clientes cresce (de 229.69 Mbps com 1 cliente para
  198.34 Mbps com 10). A queda total do cenário de 1 para 10 clientes é de cerca
  de 14%. Adicionar clientes **não aumenta** a vazão total; apenas reparte o
  mesmo total entre mais conexões, de forma desigual rodada a rodada.

- **Limite máximo observado:** ~230 Mbps (pico com 1 cliente). O teto agregado se
  mantém nessa ordem de grandeza em todos os cenários.

- **Gargalos identificados:**
  - **Enlace Wi-Fi (gargalo principal):** o meio sem fio é compartilhado e
    half-duplex. A soma da vazão não escala com o nº de clientes porque todos
    disputam o mesmo canal de rádio — este é o fator que impõe o teto de ~200–230
    Mbps.
  - **Leve degradação com mais clientes:** a queda suave (~14%) ao ir de 1 para
    10 clientes reflete o aumento da contenção no canal e o overhead de
    coordenação de mais fluxos TCP simultâneos.
  - **CPU não é gargalo:** tanto o servidor (Apple M4, 10 núcleos) quanto o
    cliente (i5-8350U) têm folga de sobra para ~200 Mbps; o uso ficou longe da
    saturação.
  - **Camada WSL 2 (NAT/virtualização):** adiciona overhead no lado do cliente,
    mas é secundário frente ao Wi-Fi.

- **Conclusões:** a aplicação transmite de forma eficiente e o servidor
  multithread contabiliza corretamente os bytes de todas as conexões. O limite de
  capacidade observado é determinado pelo enlace Wi-Fi compartilhado, não pelo
  código, pela CPU ou pelos buffers. O número de clientes simultâneos redistribui
  a banda disponível sem ampliá-la, com degradação pequena e previsível até 10
  clientes.

