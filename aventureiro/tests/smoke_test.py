#!/usr/bin/env python3
"""
Fuzz/smoke test do Pacote 9 (ver management/backlog/09-main-testes.md e
secao 6 do handover): abre o binario via pexpect e manda teclas aleatorias
por varias partidas, falhando se o processo crashar (exit code != 0 ou
morrer por sinal, ex.: SIGSEGV). Nao espera o jogo terminar sozinho -
poucas sessoes de 60 teclas aleatorias chegam a morte/vitoria por acaso;
sobreviver ao orcamento de fuzzing sem crashar ja conta como sessao OK.

O alfabeto de teclas cobre nao so os digitos 0-9 (os dez comandos), mas
tambem as letras que os sub-prompts interativos de game.c exigem: N/S/L/O
(direcao do comando Mover), S/I/A (postura do comando Comunicar-se), E/L/D
(escolha em sala escura do comando Examinar), e H (pseudo-comando de ajuda
do Pacote 11, que ui_ler_comando aceita a qualquer momento do loop
principal). Um fuzzer so-digitos travaria deterministicamente na primeira
vez que sorteasse o comando Mover, ja que esse sub-prompt (fiel ao
original, linha 1020 do BASIC) ignora qualquer tecla que nao seja uma das
quatro direcoes e continua esperando.
"""
import random
import sys
import time

try:
    import pexpect
except ImportError:
    print("Faltando 'pexpect'. Rode: pip install -r tests/requirements.txt", file=sys.stderr)
    sys.exit(1)

ALFABETO = "0123456789NnSsLlOoEeDdIiAaHh"
PASSOS_POR_SESSAO = 60
TOTAL_DE_PASSOS_ALVO = 200
MAX_SESSOES = 20
TIMEOUT_SEGUNDOS = 5


def jogar_uma_sessao(binario, seed, passos):
    child = pexpect.spawn(
        "/usr/bin/env",
        ["bash", "-c", f"TERM=xterm-256color {binario} --seed {seed}"],
        timeout=TIMEOUT_SEGUNDOS,
    )

    child.send(" ")  # sai da tela de titulo
    time.sleep(0.05)

    passos_dados = 0
    for _ in range(passos):
        if not child.isalive():
            break
        child.send(random.choice(ALFABETO))
        time.sleep(0.01)
        passos_dados += 1

    if child.isalive():
        # Sobreviveu ao orcamento de fuzzing sem terminar sozinho (o mais
        # comum - poucas sessoes de 60 teclas aleatorias chegam a
        # morte/vitoria por acaso). O que importa aqui e' que nao crashou;
        # nao ha por que esperar o jogo terminar naturalmente, entao mata o
        # processo. child.wait() sem timeout travaria para sempre num
        # processo que nunca sai sozinho.
        child.terminate(force=True)
        return 0, passos_dados

    child.wait()
    if child.signalstatus is not None:
        return f"sinal {child.signalstatus}", passos_dados
    return child.exitstatus, passos_dados


def main():
    binario = sys.argv[1] if len(sys.argv) > 1 else "build/aventureiro"

    total_passos = 0
    sessao = 0
    while total_passos < TOTAL_DE_PASSOS_ALVO and sessao < MAX_SESSOES:
        sessao += 1
        seed = random.randint(1, 1_000_000)
        status, passos = jogar_uma_sessao(binario, seed, PASSOS_POR_SESSAO)
        total_passos += passos
        print(f"sessao {sessao}: seed={seed} passos={passos} status={status}")

        if status not in (0, None):
            print(f"FALHA: sessao {sessao} terminou com exit code {status} (seed={seed})")
            sys.exit(1)

    if total_passos < TOTAL_DE_PASSOS_ALVO:
        print(f"FALHA: so consegui {total_passos} passos em {MAX_SESSOES} sessoes (alvo: {TOTAL_DE_PASSOS_ALVO})")
        sys.exit(1)

    print(f"OK: {total_passos} comandos aleatorios em {sessao} sessoes, sem crash nem travamento.")


if __name__ == "__main__":
    main()
