# Pacote 26 — ajuda como barra de comandos permanente na base da tela

**Tamanho:** M · **Depende de:** [Pacote 11](11-melhorias-jogabilidade.md), [Pacote 17](17-mapa-continuo.md)

## Objetivo

Hoje a ajuda (`H`) é um pseudo-comando que limpa o log e imprime a lista completa dos 10 comandos
por extenso (`mostrar_ajuda()`, [game.c](../../aventureiro/src/game.c)) — o jogador precisa lembrar
de apertar `H` toda vez que esquecer um número. Sugestão do usuário: virar uma **barra permanente na
base da tela**, sempre visível, só com palavras-chave curtas (não a frase inteira de cada comando) —
mesmo espírito do Pacote 17, que tirou o mapa de "sob demanda via tecla" pra "painel sempre visível".

## Entregáveis

- **Layout**: nova `WINDOW*` (`janela_barra`) de 1 linha (ou 2, se não couber em uma só — decidir
  com base em largura mínima) fixada na última linha do terminal, largura total (`COLS`). Isso
  reduz a altura disponível pro log/painel de mapa em 1-2 linhas — ajustar
  `janela_log`/`janela_mapa` (hoje calculadas em `ui_iniciar()`, [ui.c](../../aventureiro/src/ui.c))
  pra descontar essa altura, do mesmo jeito que o painel de mapa já desconta largura do log.
- **Conteúdo**: palavras-chave curtas por comando, não frases completas. Ex. (a validar com
  contagem de colunas real, grid 8x8 desenhando o mapa some largura à direita):
  `0:Mover 1:Atacar 2:Fugir 3:Arma 4:Falar 5:Escudo 6:Remédio 7:Status 8:Examinar 9:Teleporte`.
  Isso é bem mais longo que 80 colunas com espaçamento legível — decidir entre: abreviar mais
  (`0-Mv 1-At ...`), ou aceitar que só cabe uma live nos terminais mais largos e cair pra um
  fallback (ex. só os números "0-9" sem legenda) em terminais estreitos, igual ao painel de mapa já
  faz (Pacote 17: unir/sumir com base em largura mínima).
- **Decidir o destino do comando `H`**: remover de vez (barra já cobre a necessidade, mesmo default
  do Pacote 17 quando o mapa virou painel) ou manter como forma de ver a versão "por extenso"
  (frases completas, pra quem quiser mais contexto que só a palavra-chave). Se mantido, atualizar
  `mostrar_ajuda()` pra não duplicar informação com a barra; se removido, tirar `ui_ler_comando()`
  (`-1`) e simplificar como o Pacote 19 fez pro `M`.
- Atualizar a tela de título (`game_tela_titulo()`) pra não listar mais os comandos por extenso se a
  barra permanente já cobre isso (evitar redundância de texto).
- Terminal pequeno demais pra barra + HUD + log/mapa: mesma filosofia do Pacote 17 (não corrompe a
  tela, barra vira no-op silencioso ou reduz pro fallback "só números" antes de sumir de vez).
- `tests/smoke_test.py` (pexpect+pyte): nova verificação confirmando a barra aparece sem apertar
  nada, igual `verificar_painel_mapa_visivel` (Pacote 17) fez pro mapa.

## Critério de aceite

Jogando manualmente: a barra de comandos aparece sempre na última linha, com palavras-chave legíveis
(não frases completas), sem precisar apertar `H`. HUD, log e painel de mapa continuam funcionando
normalmente acima dela. Terminal pequeno demais tem comportamento definido (não corrompe a tela).
`tests/smoke_test.py` atualizado e passando.

**Resolvido e confirmado.** Decisões tomadas com o usuário: estilo de conteúdo = palavra curta
(`0:Mover 1:Atacar ...`, 90 colunas visíveis); comando `H` mantido (fala completa, complementando a
barra em vez de duplicá-la).

Implementado:

- `ui.c`: nova `WINDOW *janela_barra`, 1 linha fixa na base (`ALTURA_BARRA`), sem moldura (uma linha
  não comporta borda superior+inferior de `desenhar_moldura`). 3 variantes de texto em cascata
  (`escolher_texto_barra`), da mais legível pra mais compacta, escolhida pela largura do terminal:
  `BARRA_COMPLETA` (90 colunas, "0:Mover 1:Atacar ..."), `BARRA_ABREVIADA` (59 colunas, "0-Mv 1-At ...
  (H=ajuda)"), `BARRA_MINIMA` (20 colunas, "0123456789 (H=ajuda)"). Nenhuma cabendo, a barra vira
  `NULL`/no-op silencioso, mesma filosofia do painel de mapa (Pacote 17). A largura de cada variante é
  contada em **colunas visíveis, não bytes** (`largura_visivel_utf8`) — "Remédio" tem acento (2 bytes,
  1 coluna), contar bytes superestimaria a largura e rejeitaria uma variante que caberia.
  `janela_log`/`janela_mapa` (`ui_iniciar`) descontam a altura da barra do espaço disponível, do
  mesmo jeito que o painel de mapa já desconta largura do log. Conteúdo é estático (nunca muda
  durante a partida), então é escrito uma vez em `ui_iniciar`, sem precisar de uma função de redesenho
  chamada a cada turno.
- `game.c`: `H` mantido (`mostrar_ajuda()`), só ganhou uma linha extra mencionando que a barra
  permanente já cobre o resumo. Tela de título (`game_tela_titulo()`) simplificada — não repete mais
  a lista de 10 comandos por extenso (isso já está sempre visível na barra), só menciona a barra e o
  `H`.
- `tests/smoke_test.py`: nova `verificar_barra_comandos_visivel` confirma que a barra aparece sem
  apertar nada, igual `verificar_painel_mapa_visivel` fez pro mapa (Pacote 17). Verificado manualmente
  (fora do smoke test, que roda fixo em 100 colunas) que a variante abreviada aparece em 70 colunas e
  a barra desaparece de forma limpa em 15 colunas.
- **Achado incidental, corrigido**: rodando a suíte pra validar essa mudança, `verificar_atalho_setas`
  (Pacote 18) quebrou — mas por um motivo **pré-existente, não relacionado a este pacote** (confirmado
  reproduzindo o mesmo erro num worktree do último commit, sem nenhuma mudança deste pacote nem do
  25): `comando_mover` recusa mover em **qualquer** direção se houver tripulante vivo na sala atual
  (`combat.c:369-374`, fidelidade ao original — "não dê as costas"), e o teste de setas só prevía 2
  desfechos (moveu, ou "não há saída"), sem esse terceiro caso. Com seed fixa (1), a primeira seta
  entra numa sala com tripulante vivo, e as demais então caem nesse terceiro caso. Corrigido: teste
  aceita as 3 mensagens válidas de `comando_mover`; e como uma seta repetindo o mesmo aviso produz uma
  tela **byte-idêntica** à anterior (não dá pra esperar por "mudou"), a checagem de resposta virou um
  dreno de tempo fixo (`drenar_por`) em vez de comparação de conteúdo.

Verificação: build limpo via CMake (`-Wall -Wextra -Werror`, sem warnings). `ctest`/`smoke_test.py`
rodado 4x seguidas sem falha (painel de mapa, barra de comandos, setas, e ~240-260 comandos de fuzz
por rodada). Confirmado visualmente com pexpect+pyte em 100x30 (barra completa), 70x24 (barra
abreviada) e 15x24 (barra ausente, tela não corrompe).
