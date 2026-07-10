# Pacote 30 — bug: HUD corrompe em terminal estreito (sem checagem de largura mínima)

**Tamanho:** S · **Depende de:** [Pacote 7](07-ui.md)

## Objetivo

Achado incidental durante a verificação do Pacote 28 (redimensionamento): ao testar o jogo num
terminal bem estreito (30 colunas), o HUD (`janela_hud`, [ui.c](../../aventureiro/src/ui.c)) sai
corrompido — texto de uma linha vaza pra dentro da próxima, embaralhando com a moldura e com o log
logo abaixo:

```
┌────────────────────────────┐
│ Vida: 20    Energia: 200   D
inArma: Pistola Laser
    Escudo: desligado  Medicam
```

Confirmado que **não é causado pelo Pacote 28**: reproduz igual lançando o jogo direto em 30x24 (sem
nenhum resize envolvido) — testado num worktree limpo antes de qualquer mudança de resize. Causa:
`janela_hud` é criada em `ui_iniciar()`/`recriar_janelas()` sem nenhuma checagem de largura mínima
(diferente do painel de mapa e da barra de comandos, que têm `cabe_painel`/`cabe_barra` e viram no-op
limpo em terminal pequeno demais, Pacotes 17/26). O conteúdo impresso (`ui_desenhar_hud`,
`mvwprintw(janela_hud, 2, 2, "Arma: %-24s  Escudo: %s  Medicamentos: %d", ...)`) tem até 66 colunas de
texto - numa janela mais estreita que isso (sem `scrollok`), o ncurses quebra o texto que não coube
pra linha de baixo **dentro da mesma janela de 4 linhas**, sobrepondo o HUD com ele mesmo.

## Entregáveis

- Decidir um comportamento pra terminal estreito demais pro HUD, na mesma linha do que já existe pro
  painel de mapa/barra: opções incluem (a) truncar o texto do HUD na largura disponível (`%.*s` ou
  cortar a string antes de `mvwprintw`, perdendo informação mas sem corromper); (b) quebrar o HUD em
  mais linhas conforme a largura (aumentar `ALTURA_HUD` dinamicamente); (c) definir uma largura
  mínima abaixo da qual o jogo recusa iniciar com uma mensagem clara, em vez de rodar com a tela
  quebrada silenciosamente.
- Aplicar a mesma decisão em `recriar_janelas` (Pacote 28) pra que isso também não regrida num
  resize que encolha abaixo do mínimo.
- `tests/smoke_test.py`: verificação num terminal estreito (a decidir o limiar exato) confirmando que
  o HUD não sobrepõe suas próprias linhas.

## Critério de aceite

Lançando o jogo (ou redimensionando pra) um terminal mais estreito que o necessário pro HUD: o HUD
não corrompe/sobrepõe texto - trunca, quebra em mais linhas, ou o jogo avisa e recusa iniciar,
dependendo da opção escolhida. Terminais com largura confortável continuam iguais a hoje.
`tests/smoke_test.py` continua passando, incluindo o novo caso.
