# Pacote 12 — Fidelidade às fórmulas originais (cura e medicamento)

**Tamanho:** M · **Depende de:** [Pacote 10](10-polimento.md)

## Objetivo

O Pacote 10 comparou `combat.c` linha a linha com `aventureiro.p.bas` (a listagem original
decodificada, já presente no repo) e corrigiu dois desvios pequenos e isolados (chance de dinheiro
no loot pós-combate, redução de dano pelo escudo). Sobraram dois desvios que exigem mudar o modelo
de dados do jogador, por isso ficaram para este pacote — decisão do usuário no Pacote 10: preferir
fidelidade ao original em vez de "valores razoáveis" livres, porque testou o jogo original
extensivamente na época e confia no balanceamento dele.

## Entregáveis

- **Cura parcial (linha 4070-4080):** `LET BP=BP+7+INT(RND*3+1)` seguido de clamp em 20
  (`IF BP>20 THEN LET BP=20`) — cura **8 a 10 pontos**, não a vida cheia. Hoje
  `jogador_usar_medicamento` (`player.c`) seta `jogador->vida = vida_maxima` diretamente. Trocar
  para a fórmula original, mantendo o clamp no `vida_maxima` vindo de `config.json`.
- **Medicamento como contador (não booleano):** no original, `M` é uma variável numérica que
  acumula (`LET M=M+1` na linha 1850 ao saquear corpo; `LET M=M+((X>.4)+2*(...)+3*(...))` na linha
  5340 ao examinar sala, podendo ganhar 1 a 3 de uma vez; `LET M=M-1` na linha 4095 ao usar). Hoje
  `Jogador::tem_medicamento` é um `bool`. Trocar para `int num_medicamentos`, atualizando:
  - `types.h` (`Jogador::tem_medicamento` → `num_medicamentos`)
  - `player.c`/`player.h` (`jogador_usar_medicamento` decrementa em vez de zerar)
  - `combat.c` (todo lugar que seta `tem_medicamento = true` deve incrementar; comandos que
    exibem/checam o campo)
  - `ui.c` (HUD e Situação devem mostrar a contagem, não "sim/nao")
- **Distribuição não uniforme de item na sala** — decisão do usuário: manter fiel ao original em vez
  da simplificação uniforme. `comando_examinar_sala` agora replica os pesos exatos: cargas de
  energia 50%/30%/20% para 1/2/3 cargas (linha 5270); medicamentos 60%/25%/15% para 1/2/3 de uma vez
  (linha 5340); dinheiro via `INT(50+RND*RND*RND*2E3)` (linha 5370, cubo de `RND` enviesado para
  valores baixos). Implementado com a nova `sorteio_uniforme()` (`util.h`).
- **Extra encontrado durante a implementação:** usar medicamento com sucesso conta como uma rodada
  plena no original (linha 4100: `GOTO 6500`) — se houver tripulante vivo na sala, ele reage como
  depois de um ataque ou fuga. `comando_usar_medicamento` (`combat.h`/`combat.c`) passou a receber
  `Mapa`/`BaseDeDados` e chamar `reacao_tripulante_apos_turno` no caminho de sucesso.

## Critério de aceite

Confirmado por teste isolado (`jogador_usar_medicamento`, sem UI): curar recupera 8 a 10 pontos por
vez (nunca a vida cheia de uma vez), respeita o teto de vida máxima, e decrementa o contador de
medicamentos só quando cura de fato (não decrementa se já estava com vida cheia ou sem
medicamento). HUD/Situação mostram a contagem de medicamentos. Build limpo e smoke test
(`tests/smoke_test.py`) sem crash.
