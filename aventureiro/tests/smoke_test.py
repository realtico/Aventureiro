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
(escolha em sala escura do comando Examinar), H (pseudo-comando de ajuda
do Pacote 11) e M (pseudo-comando de mapa em tela cheia do Pacote 30),
aceitos a qualquer momento do loop principal por ui_ler_comando. Um fuzzer
so-digitos travaria deterministicamente na primeira vez que sorteasse o
comando Mover, ja que esse sub-prompt (fiel ao original, linha 1020 do
BASIC) ignora qualquer tecla que nao seja uma das quatro direcoes e
continua esperando. As sequencias de escape das setas (Pacote 18) tambem
entram no alfabeto, ver ESCAPE_SETAS. Ctrl-L ('\\x0c', Pacote 28 -
pseudo-comando -2 que forca resincronizacao de tela) tambem entra, sem
exigir nenhum resize de verdade pra ser exercitado - so' confirma que
apertar a tecla sozinha nunca crasha o loop principal.
"""
import json
import random
import shutil
import sys
import tempfile
import time

try:
    import pexpect
except ImportError:
    print("Faltando 'pexpect'. Rode: pip install -r tests/requirements.txt", file=sys.stderr)
    sys.exit(1)

try:
    import pyte
except ImportError:
    print("Faltando 'pyte'. Rode: pip install -r tests/requirements.txt", file=sys.stderr)
    sys.exit(1)

# Sequencias de escape das setas sob TERM=xterm-256color com o keypad em
# modo aplicacao (o que ncurses liga ao iniciar, via smkx) - confirmado com
# `TERM=xterm-256color infocmp -1` (kcuu1/kcud1/kcuf1/kcub1 = \EOA/\EOB/
# \EOC/\EOD, formato SS3, nao a variante \E[A mais comum fora do modo
# aplicacao). Mesma TERM usada em todo o resto deste arquivo.
ESCAPE_SETAS = {"cima": "\x1bOA", "baixo": "\x1bOB", "direita": "\x1bOC", "esquerda": "\x1bOD"}

ALFABETO = list("0123456789NnSsLlOoEeDdIiAaHhMm") + list(ESCAPE_SETAS.values()) + ["\x0c"]
PASSOS_POR_SESSAO = 60
TOTAL_DE_PASSOS_ALVO = 200
MAX_SESSOES = 20
TIMEOUT_SEGUNDOS = 5


def jogar_uma_sessao(binario, seed, passos):
    # AVENTUREIRO_SEM_PAUSAS=1 desliga as pausas dramaticas do Pacote 20
    # (~1s cada, ver ui_pausar_dramatico em ui.c) - sem isso o orcamento de
    # tempo do fuzzer (5s por sessao) estoura sempre que sortear Examinar ou
    # entrar numa sala com tripulante.
    child = pexpect.spawn(
        "/usr/bin/env",
        ["bash", "-c", f"TERM=xterm-256color AVENTUREIRO_SEM_PAUSAS=1 {binario} --seed {seed}"],
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


def _sessao_com_tela(binario, seed=1, cols=100, linhas=30, extra_args=""):
    """
    Spawna o jogo com um pty de tamanho fixo e devolve (child, conteudo_atual,
    esperar) prontos pra verificacoes de conteudo de tela via pyte. Usado
    pelas verificacoes de painel de mapa (Pacote 17) e atalho de setas
    (Pacote 18) - a fuzz loop principal (jogar_uma_sessao) nao usa isso, so'
    quer saber se o processo crasha, nunca o que esta na tela.

    'esperar' faz polling (nao um unico read apos sleep fixo): a tecla
    enviada antes do processo terminar de inicializar o ncurses (cbreak())
    pode se perder no modo canonico/line-buffered padrao do pty, e um
    redesenho grande (HUD + painel de mapa) pode chegar em varios pedacos
    pelo pty - esperar ativamente por um texto conhecido, com um dreno extra
    apos a condicao bater, evita tanto o falso negativo de tecla perdida
    quanto o de frame incompleto.
    """
    tela = pyte.Screen(cols, linhas)
    stream = pyte.Stream(tela)
    # AVENTUREIRO_SEM_PAUSAS=1: mesmo motivo do jogar_uma_sessao acima - as
    # verificacoes de painel/setas tambem entram em salas com tripulante ou
    # examinam a sala, e as pausas dramaticas do Pacote 20 empurrariam essas
    # esperas pra perto (ou alem) do timeout de cada esperar().
    child = pexpect.spawn(
        "/usr/bin/env",
        ["bash", "-c", f"stty rows {linhas} cols {cols}; TERM=xterm-256color AVENTUREIRO_SEM_PAUSAS=1 {binario} --seed {seed} {extra_args}"],
        dimensions=(linhas, cols),
        encoding="utf-8",
        codec_errors="replace",
        timeout=TIMEOUT_SEGUNDOS,
    )

    def conteudo_atual():
        return "\n".join(tela.display)

    def esperar(condicao, descricao, timeout=TIMEOUT_SEGUNDOS):
        fim = time.time() + timeout
        while time.time() < fim:
            try:
                dados = child.read_nonblocking(size=200000, timeout=0.2)
                stream.feed(dados)
            except pexpect.exceptions.TIMEOUT:
                pass
            if condicao():
                for _ in range(5):
                    try:
                        dados = child.read_nonblocking(size=200000, timeout=0.1)
                        stream.feed(dados)
                    except pexpect.exceptions.TIMEOUT:
                        break
                return
        print(f"FALHA: {descricao}\n{conteudo_atual()}", file=sys.stderr)
        child.close(force=True)
        sys.exit(1)

    def drenar_por(segundos):
        """Le por um tempo fixo, sem exigir que o conteudo mude - usada onde
        uma resposta valida pode ser identica a anterior (ver
        verificar_atalho_setas)."""
        fim = time.time() + segundos
        while time.time() < fim:
            try:
                dados = child.read_nonblocking(size=200000, timeout=0.2)
                stream.feed(dados)
            except pexpect.exceptions.TIMEOUT:
                pass

    def redimensionar(linhas_novas, cols_novas):
        """Redimensiona o pty de verdade (Pacote 28) - isso dispara SIGWINCH
        no processo filho, igual um usuario arrastando a borda da janela do
        terminal. Redimensiona tambem a tela do pyte em paralelo, senao o
        parsing local ficaria fora de sincronia com o tamanho real do pty."""
        tela.resize(linhas_novas, cols_novas)
        child.setwinsize(linhas_novas, cols_novas)

    return child, conteudo_atual, esperar, drenar_por, redimensionar


def verificar_painel_mapa_visivel(binario):
    """
    Verifica com pyte (Pacote 17) que o painel de mapa fica sempre visivel
    sem precisar de tecla dedicada (o comando 'M' do Pacote 14, reintroduzido
    no Pacote 30 como fallback pra terminal estreito, nao e' necessario
    aqui - o painel ja aparece sozinho num terminal largo), e que ele se
    atualiza sozinho ao entrar numa sala nova - a fuzz loop acima so' checa
    crash, nunca conteudo de tela.
    """
    child, conteudo_atual, esperar, drenar_por, redimensionar = _sessao_com_tela(binario)

    esperar(lambda: "Boa sorte" in conteudo_atual(), "tela de titulo nunca terminou de aparecer")
    child.send(" ")  # sai da tela de titulo, nenhuma tecla de mapa necessaria
    # espera pela legenda (ultima coisa desenhada no painel, apos o grid) em
    # vez de so' "Mapa" (o titulo, desenhado primeiro) - evita falso
    # negativo por frame incompleto (ver docstring de _sessao_com_tela).
    esperar(lambda: "você" in conteudo_atual() and "teleporte" in conteudo_atual(),
            "painel de mapa nao apareceu (ou nao terminou de desenhar) sem apertar nada")
    if "Mapa" not in conteudo_atual():
        print("FALHA: titulo do painel ('Mapa') nao aparece:\n" + conteudo_atual(), file=sys.stderr)
        child.close(force=True)
        sys.exit(1)
    if "@" not in conteudo_atual():
        print("FALHA: posicao do jogador ('@') nao aparece no painel:\n" + conteudo_atual(), file=sys.stderr)
        child.close(force=True)
        sys.exit(1)

    conteudo_inicial = conteudo_atual()

    child.send("0")
    esperar(lambda: "Para que lado" in conteudo_atual(), "prompt de direcao do comando Mover nunca apareceu")
    for direcao in "NSLO":
        child.send(direcao)
        time.sleep(0.1)
    esperar(lambda: conteudo_atual() != conteudo_inicial, "painel nao mudou apos entrar em sala nova (deveria atualizar sozinho)")

    child.close(force=True)
    print("OK: painel de mapa permanente aparece e atualiza sozinho, sem tecla dedicada (Pacote 17).")


def verificar_barra_comandos_visivel(binario):
    """
    Verifica com pyte (Pacote 26) que a barra de comandos permanente aparece
    na base da tela sem precisar apertar H, com a variante completa de texto
    (o pty de teste usa 100 colunas - largura suficiente pra BARRA_COMPLETA,
    90 colunas visiveis, ver escolher_texto_barra em ui.c).
    """
    child, conteudo_atual, esperar, drenar_por, redimensionar = _sessao_com_tela(binario)

    esperar(lambda: "Boa sorte" in conteudo_atual(), "tela de titulo nunca terminou de aparecer")
    child.send(" ")  # sai da tela de titulo, nenhuma tecla de ajuda necessaria
    esperar(lambda: "Teleporte" in conteudo_atual(), "barra de comandos nao apareceu sem apertar nada")

    tela = conteudo_atual()
    for pedaco in ("0:Mover", "1:Atacar", "9:Teleporte"):
        if pedaco not in tela:
            print(f"FALHA: barra de comandos incompleta, '{pedaco}' nao encontrado:\n{tela}", file=sys.stderr)
            child.close(force=True)
            sys.exit(1)

    child.close(force=True)
    print("OK: barra de comandos permanente aparece sem apertar nada (Pacote 26).")


def verificar_fala_tripulante_nao_trunca(binario):
    """
    Verifica com pyte (Pacote 29) que a fala do TWIN ao encontrar um
    tripulante nao trunca/come caracteres. Usa o seed 33 - achado com uma
    busca offline (BFS sobre o mapa gerado, fora deste teste) por ser o
    tripulante com a maior mensagem combinada nos dados atuais ("Medico
    Buteriano" + "Bisturi Laser" + frase = 100 bytes, 4 acima dos 96 de
    MAX_TAMANHO_MENSAGEM) e por estar a 1 porta (Norte) da Sala de
    Teleporte - encontro garantido logo de cara, sem depender de sorte do
    fuzzing aleatorio. Se os dados de data/crew.json ou data/weapons.json
    mudarem, o pior caso pode passar a ser outro tripulante/seed - refazer a
    busca (ver management/backlog/29-twin-reporta-uma-linha-so.md).
    """
    child, conteudo_atual, esperar, drenar_por, redimensionar = _sessao_com_tela(binario, seed=33)

    esperar(lambda: "Boa sorte" in conteudo_atual(), "tela de titulo nunca terminou de aparecer")
    child.send(" ")
    esperar(lambda: "você" in conteudo_atual() and "teleporte" in conteudo_atual(),
            "painel de mapa nao apareceu antes do teste de fala do tripulante")

    child.send("\x1bOA")  # seta cima = Norte - leva direto pro Medico Buteriano (seed 33)
    esperar(lambda: "TWIN reporta" in conteudo_atual(), "fala do TWIN nunca apareceu")
    drenar_por(0.5)  # garante que as 3 linhas (com pausas entre elas) ja chegaram

    tela = conteudo_atual()
    esperadas = (
        "TWIN reporta...",
        "É um Médico Buteriano, armado com Bisturi Laser.",
        "Inteligente mas pouco perigoso",
    )
    for linha in esperadas:
        if linha not in tela:
            print(f"FALHA: fala do tripulante truncada/incompleta, '{linha}' nao encontrado:\n{tela}",
                  file=sys.stderr)
            child.close(force=True)
            sys.exit(1)

    child.close(force=True)
    print("OK: fala do TWIN aparece completa, sem truncar, no pior caso conhecido (Pacote 29).")


def verificar_sala_sem_item_examinada_marca_x(binario):
    """
    Verifica com pyte (Pacote 33) que examinar uma sala sem item (comando 8,
    resultado "Nada.") marca a sala como examinada no mapa ASCII (×) - antes
    dessa correcao, o campo usado pro simbolo era item_coletado (combat.c),
    que so' ficava true se a sala tivesse item pra coletar; uma sala vazia
    examinada nunca ganhava marca nenhuma, ficando indistinguivel de uma sala
    so' visitada de passagem (ou fugida) sem nunca ter sido vasculhada.

    Usa o seed 6: a sala a 1 porta (Norte) da Sala de Teleporte nao tem item
    (confirmado via "Nada." apos o comando 8) - achado por busca manual entre
    poucos seeds, sem precisar de BFS offline (ver Pacote 29 pra esse tipo de
    busca quando um caso especifico for necessario de novo). Se o gerador de
    mapa ou as chances de item em data/config.json mudarem, o seed pode
    deixar de servir - trocar por outro que reproduza uma sala sem item logo
    no inicio.
    """
    child, conteudo_atual, esperar, drenar_por, redimensionar = _sessao_com_tela(binario, seed=6)

    esperar(lambda: "Boa sorte" in conteudo_atual(), "tela de titulo nunca terminou de aparecer")
    child.send(" ")
    esperar(lambda: "você" in conteudo_atual() and "teleporte" in conteudo_atual(),
            "painel de mapa nao apareceu antes do teste de sala sem item")

    contagem_antes = conteudo_atual().count("×")

    child.send("\x1bOA")  # seta cima = Norte - sala sem item (seed 6)
    esperar(lambda: "Você entrou numa nova sala." in conteudo_atual(), "nao entrou na sala ao Norte")
    child.send("8")
    esperar(lambda: "Nada." in conteudo_atual(), "exame da sala sem item nunca respondeu 'Nada.'")

    child.send("\x1bOB")  # seta baixo = Sul, volta pra Sala de Teleporte
    esperar(lambda: "Você entrou numa nova sala." in conteudo_atual(), "nao voltou pra Sala de Teleporte")
    drenar_por(0.3)

    tela = conteudo_atual()
    contagem_depois = tela.count("×")
    if contagem_depois <= contagem_antes:
        print(f"FALHA: sala sem item examinada nao ganhou marca × no mapa "
              f"(antes={contagem_antes}, depois={contagem_depois}):\n{tela}", file=sys.stderr)
        child.close(force=True)
        sys.exit(1)

    child.close(force=True)
    print("OK: sala sem item marca × no mapa depois de examinada com o comando 8 (Pacote 33).")


def verificar_resize_realinha_painel(binario):
    """
    Verifica com pyte (Pacote 28) que redimensionar o terminal em runtime -
    via child.setwinsize, que dispara SIGWINCH de verdade no processo filho,
    igual arrastar a borda da janela de um terminal de verdade - faz o
    painel de mapa e a barra de comandos se realinharem ao novo tamanho, em
    vez de ficarem ancorados na posicao/dimensao antiga (ver
    verificar_e_aplicar_resize em ui.c). Usa Ctrl-L (pseudo-comando -2 em
    ui_ler_comando) pra forcar a resincronizacao sem depender do timing de
    quando o proximo wgetch() aconteceria sozinho.

    largura_painel = 19 abaixo (2*grid_size-1 + 4 de borda/respiro, ver
    ui_iniciar/recriar_janelas em ui.c) e' especifico do grid_size=8 padrao
    de data/config.json - se isso mudar, o calculo aqui precisa acompanhar.
    """
    LARGURA_PAINEL_PADRAO = 19
    LARGURA_HUD_LARGA = 72  # ui.c - abaixo disso o HUD vira ESTREITO (3 linhas, altura 5 em vez de 4)

    def coluna_borda_painel(tela_texto):
        """Acha a coluna do canto superior esquerdo do painel de mapa - None
        se o painel nao esta visivel. A linha onde o painel comeca depende
        da altura do HUD (Pacote 30): 4 num terminal largo (HUD classico de
        2 linhas), 5 num terminal mais estreito que LARGURA_HUD_LARGA (HUD
        de 3 linhas) - infere qual e' o caso pela largura da propria tela
        (linha 0 sempre tem COLS colunas)."""
        linhas_tela = tela_texto.split("\n")
        largura_tela = len(linhas_tela[0])
        linha_hud = 4 if largura_tela >= LARGURA_HUD_LARGA else 5
        if len(linhas_tela) <= linha_hud:
            return None
        indice = linhas_tela[linha_hud].find("┌")
        return indice if indice >= 0 else None

    child, conteudo_atual, esperar, drenar_por, redimensionar = _sessao_com_tela(binario, cols=100, linhas=30)

    esperar(lambda: "Boa sorte" in conteudo_atual(), "tela de titulo nunca terminou de aparecer")
    child.send(" ")
    esperar(lambda: "você" in conteudo_atual() and "teleporte" in conteudo_atual(),
            "painel de mapa nao apareceu antes do teste de resize")

    col_inicial = coluna_borda_painel(conteudo_atual())
    if col_inicial != 100 - LARGURA_PAINEL_PADRAO:
        print(f"FALHA: painel nao comecou na coluna esperada ({100 - LARGURA_PAINEL_PADRAO}, achada "
              f"{col_inicial}):\n{conteudo_atual()}", file=sys.stderr)
        child.close(force=True)
        sys.exit(1)

    # 1. Encolhe a largura (100 -> 70): painel continua cabendo, mas deve
    # encostar numa coluna mais a esquerda (COLS menor); a barra completa (90
    # colunas visiveis) nao cabe mais em 70 e deve cair pra variante
    # abreviada (ver escolher_texto_barra em ui.c). 70 < LARGURA_HUD_LARGA
    # (72), entao o HUD vira ESTREITO (altura 5 em vez de 4, Pacote 30) -
    # 25 linhas (em vez de 24) pra sobrar a mesma folga de altura_disponivel
    # que o painel (altura_painel=19) sempre teve nesse teste.
    redimensionar(25, 70)
    child.send("\x0c")  # Ctrl-L - forca resincronizacao sem esperar comando real
    drenar_por(1.0)
    col = coluna_borda_painel(conteudo_atual())
    if col != 70 - LARGURA_PAINEL_PADRAO:
        print(f"FALHA: painel nao se realinhou ao encolher pra 70 colunas (esperada "
              f"{70 - LARGURA_PAINEL_PADRAO}, achada {col}):\n{conteudo_atual()}", file=sys.stderr)
        child.close(force=True)
        sys.exit(1)
    if "0-Mv" not in conteudo_atual():
        print(f"FALHA: barra nao caiu pra variante abreviada em 70 colunas:\n{conteudo_atual()}",
              file=sys.stderr)
        child.close(force=True)
        sys.exit(1)

    # 2. Encolhe so' a altura, bem abaixo do minimo do painel, mantendo
    # largura generosa (o gargalo aqui e' altura_painel, nao
    # LARGURA_MINIMA_LOG): painel deve sumir de forma limpa, sem fragmento de
    # moldura sobrando.
    redimensionar(15, 100)
    child.send("\x0c")
    drenar_por(1.0)
    if coluna_borda_painel(conteudo_atual()) is not None:
        print(f"FALHA: painel deveria ter sumido com altura insuficiente (15 linhas):\n"
              f"{conteudo_atual()}", file=sys.stderr)
        child.close(force=True)
        sys.exit(1)

    # 3. Cresce de volta ao tamanho original: painel deve reaparecer,
    # encostado na borda direita correspondente.
    redimensionar(30, 100)
    child.send("\x0c")
    drenar_por(1.0)
    col = coluna_borda_painel(conteudo_atual())
    if col != 100 - LARGURA_PAINEL_PADRAO:
        print(f"FALHA: painel nao reapareceu ao crescer de volta pra 100 colunas (esperada "
              f"{100 - LARGURA_PAINEL_PADRAO}, achada {col}):\n{conteudo_atual()}", file=sys.stderr)
        child.close(force=True)
        sys.exit(1)

    child.close(force=True)
    print("OK: painel de mapa e barra de comandos se realinham ao redimensionar o terminal (Pacote 28).")


def verificar_atalho_setas(binario):
    """
    Verifica com pyte (Pacote 18) que uma seta do teclado move o jogador
    direto numa direcao - sem passar pelo prompt "para que lado" do comando
    Mover interativo (comando_mover_interativo, so' usado quando o jogador
    digita '0'). Manda as 4 setas em sequencia - cada uma deve produzir um dos
    3 desfechos validos de comando_mover (combat.c): moveu de fato, "Nao ha
    saida pelo ..." (sem porta nessa direcao), ou "Nao se arrisque dando as
    costas ao ..." (ha tripulante vivo na sala atual - comando_mover recusa
    qualquer movimento nesse caso, fiel ao original) - nenhuma deve mostrar o
    prompt manual.
    """
    child, conteudo_atual, esperar, drenar_por, redimensionar = _sessao_com_tela(binario)

    esperar(lambda: "Boa sorte" in conteudo_atual(), "tela de titulo nunca terminou de aparecer")
    child.send(" ")
    esperar(lambda: "você" in conteudo_atual() and "teleporte" in conteudo_atual(),
            "painel de mapa nao apareceu antes do teste de setas")

    moveu_direto = False
    for nome, seq in ESCAPE_SETAS.items():
        # Nao da pra esperar por "a tela mudou": uma vez que o jogador fica
        # preso numa sala com tripulante vivo (ha_tripulante == True), TODA
        # seta seguinte repete a mensagem "Nao se arrisque..." *identica* a
        # anterior (mesmo texto, mesma posicao) - a tela em pyte nao muda um
        # byte, mesmo com a tecla corretamente processada. Por isso so' da'
        # uma folga fixa pro pty entregar a resposta, sem exigir diferenca.
        child.send(seq)
        drenar_por(1.0)
        tela_apos = conteudo_atual()
        if "Para que lado" in tela_apos:
            print(f"FALHA: seta {nome} caiu no prompt manual em vez de mover direto:\n{tela_apos}", file=sys.stderr)
            child.close(force=True)
            sys.exit(1)
        if "Você entrou numa nova sala." in tela_apos:
            moveu_direto = True
        elif ("Não há saída pelo" not in tela_apos
              and "Não se arrisque dando as costas" not in tela_apos):
            print(f"FALHA: seta {nome} nao mostrou movimento, 'Nao ha saida' nem aviso de "
                  f"tripulante na sala:\n{tela_apos}", file=sys.stderr)
            child.close(force=True)
            sys.exit(1)

    if not moveu_direto:
        print("FALHA: nenhuma das 4 setas moveu o jogador (esperava pelo menos uma porta na sala de partida)",
              file=sys.stderr)
        child.close(force=True)
        sys.exit(1)

    child.close(force=True)
    print("OK: setas do teclado movem direto na direção, sem passar pelo prompt manual (Pacote 18).")


def _linha_borda(largura, esquerda, direita):
    return esquerda + "─" * (largura - 2) + direita


def verificar_hud_estreito_nao_corrompe(binario):
    """
    Verifica com pyte (Pacote 30) que o HUD muda pro layout de 3 linhas em
    terminal mais estreito que LARGURA_HUD_LARGA (72 colunas, ver ui.c) sem
    corromper/sobrepor texto - o bug original (management/backlog/
    30-hud-corrompe-terminal-estreito.md) fazia o texto de uma linha vazar
    pra dentro da moldura/linha seguinte. O detector aqui e' a propria borda
    inferior do HUD (linha ALTURA_HUD_ESTREITA-1, ver ui.c): se qualquer
    conteudo tivesse vazado por cima dela, a linha nao seria mais um
    "└──…──┘" limpo. Cobre os dois casos do relato original: 30 colunas (a
    descoberta inicial) e 53x29 (Termux, o alvo real de uso, ver cabecalho
    do pacote).
    """
    ALTURA_HUD_ESTREITA = 5  # ui.c - 2 bordas + 3 linhas de conteudo

    for cols, linhas, rotulo in ((30, 24, "30 colunas"), (53, 29, "53x29 (Termux)")):
        child, conteudo_atual, esperar, drenar_por, redimensionar = _sessao_com_tela(binario, cols=cols, linhas=linhas)

        esperar(lambda: "Boa sorte" in conteudo_atual(), f"[{rotulo}] tela de titulo nunca terminou de aparecer")
        child.send(" ")
        esperar(lambda: "Vida" in conteudo_atual(), f"[{rotulo}] HUD nunca apareceu")
        drenar_por(0.3)

        linhas_tela = conteudo_atual().split("\n")

        borda_superior_esperada = _linha_borda(cols, "┌", "┐")
        if linhas_tela[0] != borda_superior_esperada:
            print(f"FALHA [{rotulo}]: borda superior do HUD corrompida (esperada "
                  f"{borda_superior_esperada!r}, achada {linhas_tela[0]!r}):\n{conteudo_atual()}", file=sys.stderr)
            child.close(force=True)
            sys.exit(1)

        borda_inferior_esperada = _linha_borda(cols, "└", "┘")
        linha_borda_inferior = linhas_tela[ALTURA_HUD_ESTREITA - 1]
        if linha_borda_inferior != borda_inferior_esperada:
            print(f"FALHA [{rotulo}]: borda inferior do HUD corrompida - texto vazou de uma linha pra "
                  f"outra (esperada {borda_inferior_esperada!r}, achada {linha_borda_inferior!r}):\n"
                  f"{conteudo_atual()}", file=sys.stderr)
            child.close(force=True)
            sys.exit(1)

        for indice, pedaco in ((1, "Vida"), (2, "Arma"), (3, "Esc")):
            if pedaco not in linhas_tela[indice]:
                print(f"FALHA [{rotulo}]: linha {indice} do HUD nao contem '{pedaco}' (layout de 3 "
                      f"linhas nao aplicado corretamente):\n{conteudo_atual()}", file=sys.stderr)
                child.close(force=True)
                sys.exit(1)

        child.close(force=True)

    print("OK: HUD usa layout de 3 linhas sem corromper a moldura em 30 cols e 53x29/Termux (Pacote 30).")


def verificar_comando_mapa_tela_cheia(binario):
    """
    Verifica com pyte (Pacote 30) que o pseudo-comando 'M'/'m' (retorna -7 em
    ui_ler_comando) mostra o mapa em tela cheia via
    ui_mostrar_mapa_tela_cheia() - o fallback pro caso do painel lateral nao
    caber (cabe_painel falso, aqui forcado usando 30 colunas, bem abaixo de
    LARGURA_MINIMA_LOG=40 sozinho). Confirma tambem que fechar o overlay (ao
    apertar qualquer tecla) restaura o HUD corretamente, sem deixar o texto
    do mapa "gravado" por baixo (ver o touchwin/wrefresh forcado em
    ui_mostrar_mapa_tela_cheia).
    """
    child, conteudo_atual, esperar, drenar_por, redimensionar = _sessao_com_tela(binario, cols=30, linhas=24)

    esperar(lambda: "Boa sorte" in conteudo_atual(), "tela de titulo nunca terminou de aparecer")
    child.send(" ")
    esperar(lambda: "Vida" in conteudo_atual(), "HUD nunca apareceu antes do teste do comando M")

    if "Mapa" in conteudo_atual():
        print(f"FALHA: painel lateral apareceu em 30 colunas (deveria so' caber com M):\n{conteudo_atual()}",
              file=sys.stderr)
        child.close(force=True)
        sys.exit(1)

    child.send("m")
    esperar(lambda: "você" in conteudo_atual() and "teleporte" in conteudo_atual(),
            "mapa em tela cheia nao apareceu (ou nao terminou de desenhar) apos apertar M")
    if "Mapa" not in conteudo_atual():
        print(f"FALHA: titulo do mapa em tela cheia nao aparece apos M:\n{conteudo_atual()}", file=sys.stderr)
        child.close(force=True)
        sys.exit(1)

    child.send(" ")  # qualquer tecla fecha o overlay (wgetch sem filtro)
    esperar(lambda: "Vida" in conteudo_atual(), "HUD nao voltou apos fechar o mapa em tela cheia")
    if "Mapa" in conteudo_atual():
        print(f"FALHA: overlay do mapa continuou na tela apos apertar uma tecla pra fechar:\n{conteudo_atual()}",
              file=sys.stderr)
        child.close(force=True)
        sys.exit(1)

    child.close(force=True)
    print("OK: comando M mostra o mapa em tela cheia e fecha corretamente em terminal estreito (Pacote 30).")


def verificar_log_nao_quebra_palavra_nem_deixa_lixo(binario):
    """
    Verifica com pyte (Pacote 31) que a tela de titulo (game_tela_titulo,
    varios paragrafos longos escritos via ui_log) quebra linha entre
    palavras em terminal estreito, em vez de deixar o wrap por coluna do
    ncurses cortar palavras no meio - e que a janela de log, ao rolar
    (scrollok, mais texto que a altura disponivel), nao deixa sobra de texto
    de escritas bem anteriores vazando pra dentro de uma linha nova mais
    curta (bug relacionado mas distinto: achado depurando este pacote,
    reproduzido isolando os bytes brutos que o ncurses manda num
    pyte.Stream sem pty nenhum envolvido - ver o comentario de redrawwin()
    em ui_log, ui.c). Cobre os dois terminais do relato original do Pacote
    30 (30 cols e 53x29/Termux) comparando contra o wrap correto esperado,
    calculado a mao pro mesmo texto.
    """
    casos = {
        53: (
            "ou arma) haverá um gasto nas suas reservas de",
            "energia. Para terminar o",
            "jogo você deve retornar à Sala de Teleporte e acionar",
            "o teleporte (9).",
        ),
        30: (
            "Sempre que você utilizar algum",
            "dos seus equipamentos",
            "(lanterna, escudo",
        ),
    }

    for cols, linhas_esperadas in casos.items():
        child, conteudo_atual, esperar, drenar_por, redimensionar = _sessao_com_tela(binario, cols=cols, linhas=29)
        esperar(lambda: "Boa sorte" in conteudo_atual(), f"[{cols} cols] tela de titulo nunca terminou de aparecer")
        drenar_por(0.3)

        tela = conteudo_atual()
        linhas_tela = {l.rstrip() for l in tela.split("\n")}
        for esperada in linhas_esperadas:
            if esperada not in linhas_tela:
                print(f"FALHA [{cols} cols]: linha esperada do wrap correto nao encontrada (quebrou palavra "
                      f"no meio, ou sobrou lixo de uma escrita anterior colado nela) - {esperada!r}:\n{tela}",
                      file=sys.stderr)
                child.close(force=True)
                sys.exit(1)

        child.close(force=True)

    print("OK: tela de titulo quebra entre palavras (nao no meio) e a janela de log nao deixa lixo "
          "de escritas anteriores ao rolar, em 30 cols e 53 cols (Pacote 31).")


def verificar_quebra_linha_nao_pula_linha_extra(binario):
    """
    Verifica com pyte (achado jogando manualmente, relatado pelo usuario)
    que uma linha quebrada por escrever_com_quebra_de_palavra (ui.c,
    Pacote 31) nao deixa uma linha em branco extra quando o pedaco quebrado
    preenche a largura da janela EXATAMENTE. Em 53 colunas (Termux), a
    mensagem "Que sorte. Existe luz suficiente para examinar a sala sem
    usar a lanterna." quebra com o primeiro pedaco tendo exatamente 53
    caracteres visiveis - o ncurses ja deixa o cursor pendente de quebra
    automatica nesse caso, e um '\\n' explicito duplicava o avanco (ver
    escrever_linha_com_avanco em ui.c). Sala de Teleporte nunca e' escura
    (map.c: eh_teleporte pula o sorteio de 'escura', ver gerar_mapa) -
    examinar ela logo de saida, em qualquer seed, sempre cai nesse
    caminho, sem depender de sorte.
    """
    child, conteudo_atual, esperar, drenar_por, redimensionar = _sessao_com_tela(binario, cols=53, linhas=29)
    esperar(lambda: "Boa sorte" in conteudo_atual(), "tela de titulo nunca terminou de aparecer")
    child.send(" ")
    esperar(lambda: "Vida" in conteudo_atual(), "HUD nunca apareceu")

    child.send("8")  # Examinar - Sala de Teleporte nunca e' escura, cai direto na mensagem de sorte
    esperar(lambda: "sem usar a lanterna." in conteudo_atual(), "mensagem de sala iluminada nunca apareceu")
    drenar_por(0.3)

    linhas_tela = [l.rstrip() for l in conteudo_atual().split("\n")]
    indice_primeira = next((i for i, l in enumerate(linhas_tela) if l.endswith("para examinar a sala")), None)
    if indice_primeira is None:
        print(f"FALHA: linha 'Que sorte...para examinar a sala' nao encontrada:\n{conteudo_atual()}",
              file=sys.stderr)
        child.close(force=True)
        sys.exit(1)
    linha_seguinte = linhas_tela[indice_primeira + 1]
    if linha_seguinte != "sem usar a lanterna.":
        print(f"FALHA: linha em branco extra entre as duas metades da mensagem (esperada "
              f"'sem usar a lanterna.' logo apos, achada {linha_seguinte!r}):\n{conteudo_atual()}",
              file=sys.stderr)
        child.close(force=True)
        sys.exit(1)

    child.close(force=True)
    print("OK: quebra de linha exatamente do tamanho da janela nao pula linha em branco extra, 53 cols (Pacote 31).")


def verificar_nome_arma_longo_trunca_sem_corromper(binario):
    """
    Verifica com pyte (Pacote 31) a defesa em profundidade do HUD contra um
    nome de arma futuro mais longo que o campo suporta (truncar_visivel_utf8
    em ui.c, ui_desenhar_hud) - MAX_NOME permite ate 47 colunas, bem mais
    que qualquer nome real hoje (max 16), entao usa um data-dir temporario
    com a arma inicial (id 2, "Pistola Laser") renomeada bem mais longa que
    isso, sem tocar em data/weapons.json de verdade. Sem a truncagem, esse
    nome estouraria janela_hud (sem scrollok) e reproduziria a mesma
    corrupcao que o Pacote 30 corrigiu, so' que disparada por dado em vez de
    por terminal estreito.
    """
    dir_original = "data"
    dir_temp = tempfile.mkdtemp(prefix="aventureiro_teste_nome_longo_")
    try:
        for arquivo in ("config.json", "rooms.json", "weapons.json", "crew.json"):
            shutil.copy(f"{dir_original}/{arquivo}", f"{dir_temp}/{arquivo}")

        with open(f"{dir_temp}/weapons.json", encoding="utf-8") as f:
            dados = json.load(f)
        nome_longo = "Espingarda de Plasma Ultra Refinadíssima Extra Longa"
        encontrada = False
        for arma in dados["armas"]:
            if arma["id"] == 2:  # arma inicial, ver player.c:jogador_iniciar
                arma["nome"] = nome_longo
                encontrada = True
        if not encontrada:
            print("FALHA: arma id 2 (esperada 'Pistola Laser', a inicial) nao encontrada em weapons.json - "
                  "conferir jogador_iniciar em player.c", file=sys.stderr)
            sys.exit(1)
        with open(f"{dir_temp}/weapons.json", "w", encoding="utf-8") as f:
            json.dump(dados, f, ensure_ascii=False)

        ALTURA_HUD_ESTREITA = 5  # ui.c - ambos os terminais deste teste sao mais estreitos que LARGURA_HUD_LARGA

        for cols, linhas in ((53, 29), (30, 24)):
            child, conteudo_atual, esperar, drenar_por, redimensionar = _sessao_com_tela(
                binario, cols=cols, linhas=linhas, extra_args=f"--data-dir {dir_temp}")
            esperar(lambda: "Boa sorte" in conteudo_atual(), f"[{cols} cols] tela de titulo nunca apareceu")
            child.send(" ")
            esperar(lambda: "Arma" in conteudo_atual(), f"[{cols} cols] HUD nunca apareceu")
            drenar_por(0.3)

            tela = conteudo_atual()
            linhas_tela = tela.split("\n")
            borda_inferior_esperada = _linha_borda(cols, "└", "┘")
            linha_borda_inferior = linhas_tela[ALTURA_HUD_ESTREITA - 1]
            if linha_borda_inferior != borda_inferior_esperada:
                print(f"FALHA [{cols} cols]: HUD corrompido com nome de arma longo - borda inferior nao "
                      f"esta limpa (esperada {borda_inferior_esperada!r}, achada {linha_borda_inferior!r}):\n"
                      f"{tela}", file=sys.stderr)
                child.close(force=True)
                sys.exit(1)
            if "…" not in tela:
                print(f"FALHA [{cols} cols]: nome de arma longo nao foi truncado (esperava '…' no HUD):\n{tela}",
                      file=sys.stderr)
                child.close(force=True)
                sys.exit(1)

            child.close(force=True)
    finally:
        shutil.rmtree(dir_temp, ignore_errors=True)

    print("OK: nome de arma mais longo que o campo do HUD suporta e' truncado com '…' em vez de corromper "
          "a moldura, em 30 cols e 53 cols (Pacote 31).")


def main():
    binario = sys.argv[1] if len(sys.argv) > 1 else "build/aventureiro"

    verificar_painel_mapa_visivel(binario)
    verificar_barra_comandos_visivel(binario)
    verificar_fala_tripulante_nao_trunca(binario)
    verificar_sala_sem_item_examinada_marca_x(binario)
    verificar_resize_realinha_painel(binario)
    verificar_atalho_setas(binario)
    verificar_hud_estreito_nao_corrompe(binario)
    verificar_comando_mapa_tela_cheia(binario)
    verificar_log_nao_quebra_palavra_nem_deixa_lixo(binario)
    verificar_nome_arma_longo_trunca_sem_corromper(binario)
    verificar_quebra_linha_nao_pula_linha_extra(binario)

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
