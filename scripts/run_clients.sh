# Lança N clientes de medição em paralelo contra o servidor e junta a saída CSV.
# Rodar na máquina dos clientes (WSL). O servidor deve já estar no ar, esperando
# exatamente N clientes.
#
# Uso:
#   ./run_clients.sh <IP_SERVIDOR> <PORTA> <N_CLIENTES> [DURACAO_S] [BLOCO] [REP]

set -u

IP="${1:?informe o IP do servidor}"
PORT="${2:?informe a porta}"
N="${3:?informe o numero de clientes}"
DURATION="${4:-15}"
BLOCK="${5:-65536}"
REP="${6:-1}"

CLIENT_BIN="./throughput_client"

if [ ! -x "$CLIENT_BIN" ]; then
    echo "Erro: $CLIENT_BIN nao encontrado ou nao executavel." >&2
    echo "Compile com: gcc -std=c11 -D_POSIX_C_SOURCE=200809L -O2 -Wall -Wextra chat_client.c -o throughput_client" >&2
    exit 1
fi

OUT="resultado_${N}clientes_rep${REP}.csv"
: > "$OUT"
TMPDIR_RUN="$(mktemp -d)"

echo "== Cenario: $N cliente(s) | rodada $REP | duracao ${DURATION}s | bloco ${BLOCK}B =="
echo "Servidor: $IP:$PORT"
echo "Lancando $N cliente(s) em paralelo..."

pids=()
for i in $(seq 1 "$N"); do
    # Cada cliente escreve em seu proprio arquivo, para evitar que escritas
    # concorrentes no mesmo arquivo se atropelem (10 processos em paralelo).
    "$CLIENT_BIN" "$IP" "$PORT" "$DURATION" "$BLOCK" "$i" > "$TMPDIR_RUN/cli_$i.csv" &
    pids+=($!)
done

# Espera todos terminarem.
fail=0
for pid in "${pids[@]}"; do
    if ! wait "$pid"; then
        fail=1
    fi
done

# Junta os arquivos individuais na ordem dos ids.
for i in $(seq 1 "$N"); do
    cat "$TMPDIR_RUN/cli_$i.csv" >> "$OUT"
done
rm -rf "$TMPDIR_RUN"

# Conferencia do numero de linhas coletadas.
linhas=$(grep -c '^CLIENT,' "$OUT")
if [ "$linhas" -ne "$N" ]; then
    echo "AVISO: esperava $N linhas CSV, coletei $linhas." >&2
    fail=1
fi

echo
echo "CSV dos clientes salvo em: $OUT"
echo "--- conteudo ---"
cat "$OUT"
echo "----------------"

if [ "$fail" -ne 0 ]; then
    echo "AVISO: algum cliente saiu com erro. Confira a saida acima." >&2
fi

# Soma local (conferencia): coluna 3 = bytes, coluna 4 = segundos.
awk -F, '
    $1=="CLIENT" { bytes += $3; if ($4 > maxt) maxt = $4 }
    END {
        if (maxt > 0)
            printf "Conferencia local: %.0f bytes em ~%.3f s => %.2f Mbps (soma dos clientes)\n", bytes, maxt, bytes*8/maxt/1e6
    }
' "$OUT"
