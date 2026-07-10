# Pacote 28 — bug: painéis não se realinham ao redimensionar o terminal

**Tamanho:** M · **Depende de:** [Pacote 17](17-mapa-continuo.md), [Pacote 26](26-barra-comandos.md)

## Objetivo

Usuário redimensionou a janela do terminal com o jogo já rodando. No próximo redesenho, a moldura do
HUD (topo) continuou íntegra, mas o painel de mapa **não foi para o canto** — ficou desalinhado, sem
acompanhar o novo tamanho da tela.

Causa: `ui_iniciar()` ([ui.c:65-107](../../aventureiro/src/ui.c#L65-L107)) cria `janela_hud`,
`janela_log` e `janela_mapa` **uma única vez**, com posição/tamanho calculados a partir de
`COLS`/`LINES` lidos naquele instante (`largura_log`/`largura_painel`, [ui.c:99-106](../../aventureiro/src/ui.c#L99-L106)).
Não existe nenhum tratamento de `SIGWINCH`/`KEY_RESIZE` no projeto (confirmado por busca no
repositório inteiro — zero ocorrências). Quando o terminal é redimensionado, o ncurses atualiza
`LINES`/`COLS` internamente e redimensiona a `stdscr`, mas as sub-janelas já criadas via `newwin`
**não** são movidas nem redimensionadas automaticamente — elas mantêm a origem e o tamanho antigos.
Isso explica o sintoma relatado: `janela_hud` nasce ancorada em `(0,0)` ([ui.c:79](../../aventureiro/src/ui.c#L79)),
então mesmo com a largura antiga ela continua parecendo uma moldura íntegra no canto superior
esquerdo (só não usa a largura nova). Já `janela_mapa` nasce em `(0, largura_log)`
([ui.c:105](../../aventureiro/src/ui.c#L105)) — uma coluna calculada a partir do `COLS` **antigo** — então
depois do resize essa coluna já não corresponde à borda direita do terminal novo, e o painel some do
canto (sobra ou falta espaço à direita, dependendo se a janela cresceu ou encolheu).

## Entregáveis

- **Checar o tamanho real do terminal a cada redraw, sem depender de `KEY_RESIZE`**: o `SIGWINCH` do
  ncurses só atualiza `LINES`/`COLS` internamente na próxima chamada de `wgetch()` — e o jogo tem três
  pontos diferentes que bloqueiam em `wgetch` (`ui_ler_comando`, `ui_aguardar_tecla`, `ui_ler_numero`,
  [ui.c:209-258](../../aventureiro/src/ui.c#L209-L258)), então interceptar `KEY_RESIZE` exigiria
  embrulhar os três. Como `ui_desenhar_hud`/`ui_desenhar_mapa` já rodam **sem condição a cada turno**
  (decisão do Pacote 17: "redesenha sempre, redesenhar é barato"), é mais simples perguntar o tamanho
  real via `ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)` direto no fd (não depende do `COLS`/`LINES` do
  ncurses, que podem estar desatualizados) no início de um desses redraws, comparar com o último
  tamanho conhecido (cache estático em `ui.c`) e, se mudou, acionar o recálculo de layout abaixo antes
  de desenhar. Sem precisar tocar nos três pontos de leitura de tecla.
- **Recalcular o layout**: extrair a lógica de dimensionamento hoje só em `ui_iniciar()`
  (`largura_grid`/`altura_painel`/`cabe_painel`/`largura_log`/`escolher_texto_barra`,
  [ui.c:90-153](../../aventureiro/src/ui.c#L90-L153) — o Pacote 26 acrescentou a barra de comandos
  como uma quarta janela, `janela_barra`, também dimensionada só uma vez em `ui_iniciar`) pra uma
  função reaproveitável (ex. `recriar_janelas(int tamanho_mapa)`), chamada tanto por `ui_iniciar()`
  quanto pela checagem acima. Ao disparar: `resizeterm(ws.ws_row, ws.ws_col)` pra sincronizar
  `LINES`/`COLS`/`stdscr` do ncurses com o tamanho real lido via `ioctl`, depois `delwin()` nas
  **quatro** janelas atuais (`janela_hud`, `janela_log`, `janela_mapa`, `janela_barra`) e recriar com
  `newwin()` nas novas coordenadas — preservando a mesma regra de fallback já existente (`cabe_painel`
  /`escolher_texto_barra`: painel de mapa e barra de comandos somem se não couberem, voltam se crescer
  de novo).
- Documentar a consequência conhecida: recriar `janela_log` descarta o scrollback acumulado (não há
  como "reimprimir" o que já rolou) — decidir se isso é aceitável ou se vale guardar as últimas N
  linhas pra reimprimir após recriar.
- **Tecla no-op pra forçar o redesenho sem esperar o próximo turno**: como o redraw só roda no topo
  do `game_loop` ([game.c:183-185](../../aventureiro/src/game.c#L183-L185)), antes de bloquear em
  `ui_ler_comando()`, se o resize acontecer enquanto o jogo está parado esperando o jogador digitar um
  comando, os painéis só se realinhariam depois da próxima tecla real. Já existe o precedente exato
  pra isso: `ui_ler_comando()` retorna sentinelas negativas pra "pseudo-comando que não consome
  rodada" (`-1` = ajuda, tratado em [game.c:187-190](../../aventureiro/src/game.c#L187-L190) com um
  `continue` simples de volta ao topo do loop). Adicionar uma tecla nova nesse mesmo esquema — ex.
  Ctrl-L (`'\f'`/ASCII 12, convenção clássica de terminal pra "redesenhar tela", livre de conflito com
  os dígitos/setas já mapeados) devolvendo `-2` (livre desde que o Pacote 17 removeu o comando `M`) —
  cujo único efeito em `game_loop` é `continue`: cai direto no redraw do topo do loop, que já contém a
  checagem de tamanho acima. Dá ao jogador um jeito imediato de "sincronizar" a tela logo após
  redimensionar, sem precisar esperar o próximo comando de verdade nem introduzir `KEY_RESIZE`.
- `tests/smoke_test.py`: `pyte`/`pexpect` conseguem simular resize de pty (checar API disponível —
  `TIOCSWINSZ` via `fcntl`/`struct.pack`, ou suporte nativo do `pexpect`). Adicionar verificação que
  redimensiona o pty em runtime, envia uma tecla qualquer (dispara o próximo redraw) e confirma que o
  painel de mapa aparece encostado na borda direita nova (não na posição antiga).

## Critério de aceite

Jogando manualmente: redimensionar a janela do terminal (crescer e encolher, inclusive cruzando o
limiar de `cabe_painel`) e apertar Ctrl-L — HUD, log e painel de mapa se realinham imediatamente ao
novo tamanho, sem consumir um turno, com o painel de mapa sempre encostado no canto direito. Redimensionar e apertar
qualquer outra tecla de comando também realinha (via o redraw normal do próximo turno). Terminal
encolhido abaixo do mínimo faz o painel sumir de forma limpa (sem corromper a tela); crescendo de
volta, o painel reaparece. `tests/smoke_test.py` continua passando, incluindo o novo caso de resize.

**Resolvido e confirmado.** Implementado exatamente como planejado, sem precisar de `KEY_RESIZE`:

- `ui.c`: layout de `ui_iniciar()` extraído pra `recriar_janelas(int tamanho_mapa)` (reaproveitável),
  com `destruir_janelas()` fazendo os 4 `delwin()` (também usada por `ui_encerrar()`, eliminando a
  duplicação). Nova `verificar_e_aplicar_resize()`: lê o tamanho real do terminal via
  `ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)` (não depende do `LINES`/`COLS` do ncurses, que só atualizam
  na próxima `wgetch()` após o `SIGWINCH`); se mudou desde a última vez (cache em
  `ultima_altura`/`ultima_largura`), chama `resizeterm()` pra sincronizar o ncurses e
  `recriar_janelas()` com o layout novo. Chamada no início de `ui_desenhar_hud`/`ui_desenhar_mapa`
  (que já rodam sem condição a cada turno, Pacotes 17/26) — sem tocar nos 3 pontos de leitura de
  tecla.
- `ui_ler_comando()`: Ctrl-L (`'\f'`) retorna `-2` (pseudo-comando, livre desde o Pacote 17). `game.c`
  (`game_loop`): `comando == -2` só faz `continue` — cai direto no redraw do topo do loop, que já
  contém a checagem de resize.
- Consequência aceita (documentada no código): recriar `janela_log` descarta o scrollback acumulado —
  ok, o log já é limpo a cada comando mesmo (`ui_limpar_log`).
- `tests/smoke_test.py`: `_sessao_com_tela` ganhou `redimensionar(linhas, cols)` (usa
  `child.setwinsize`, que dispara `SIGWINCH` de verdade no processo filho, igual arrastar a borda de
  um terminal real). Nova `verificar_resize_realinha_painel`: encolhe largura (100→70, painel
  realinha e barra cai pra variante abreviada), encolhe só altura bem abaixo do mínimo do painel
  (painel some limpo), cresce de volta (painel reaparece na borda certa). Ctrl-L também entrou no
  alfabeto do fuzzer (`ALFABETO`).
- **Achado incidental, não corrigido aqui**: testando terminais estreitos, o HUD corrompe (texto
  vaza entre as 4 linhas) em larguras pequenas — confirmado que é **pré-existente**, reproduz igual
  sem nenhum resize envolvido (lançando direto em 30 colunas), porque `janela_hud` nunca teve
  checagem de largura mínima como o painel/barra têm. Registrado à parte, ver
  [Pacote 30](30-hud-corrompe-terminal-estreito.md).

Verificação: build limpo (`-Wall -Wextra -Werror`). Testado manualmente com pexpect+pyte, resize de
pty de verdade (`child.setwinsize`) + Ctrl-L: 100x30 → 70x24 (painel e barra realinham), → 100x15
(painel some limpo, HUD/barra intactos) → 100x30 (painel reaparece). `ctest`/`smoke_test.py` completo
rodado 5x seguidas sem falha.
