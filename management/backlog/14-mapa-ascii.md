# Pacote 14 — Mapa ASCII (comando extra)

**Tamanho:** S · **Depende de:** [Pacote 10](10-polimento.md)

## Objetivo

O original era 100% textual, sem visualização de mapa (decisão do Pacote 0, seção 7 do handover).
No Pacote 10 o usuário pediu para adicionar um mapa ASCII das salas já visitadas, acionado por um
comando dedicado fora dos dez comandos originais (tecla `M`, análogo ao `h`/`H` de ajuda do
[Pacote 11](11-melhorias-jogabilidade.md)) — não é um comando novo do jogo original, é uma
conveniência de UI que não deve interferir na fidelidade das regras.

## Entregáveis

- **Rastrear salas visitadas:** novo campo em `Celula` (`types.h`), ex. `bool visitada`, setado ao
  entrar na sala (`entrar_em_sala`, `combat.c`) e no nascimento do jogador na Sala de Teleporte.
- **Desenho do mapa (`ui.c`):** nova função (ex. `ui_desenhar_mapa`) que renderiza o grid
  `grid_size x grid_size`, distinguindo pelo menos três estados por célula: não visitada (oculta),
  visitada, e posição atual do jogador. Não revelar tipo de sala/conteúdo de células não visitadas.
- **Novo pseudo-comando em `game_loop` (`game.c`):** aceitar `m`/`M` (igual ao tratamento de `h`/`H`
  do Pacote 11) para mostrar o mapa e voltar ao jogo **sem consumir uma rodada** — sem custo de
  energia, sem contar como turno para nada que dependa disso (contra-ataque, drenagem de escudo
  etc.).
- Atualizar a lista de comandos exibida em `game_tela_titulo` (ou um texto de ajuda separado) para
  mencionar a tecla `M` como extra, deixando claro que os dez comandos originais continuam sendo só
  0-9.

## Critério de aceite

Jogando manualmente: apertar `M` a qualquer momento do loop principal mostra um mapa textual com as
salas já visitadas e a posição atual, sem alterar vida/energia/dinheiro/posição do jogador; salas
nunca visitadas não aparecem com tipo/conteúdo revelado.
