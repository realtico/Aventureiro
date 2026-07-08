# Pacote 13 — Fuga/ataque e perseguição fiéis ao original

**Tamanho:** M · **Depende de:** [Pacote 10](10-polimento.md)

## Objetivo

O Pacote 0 simplificou a reação do tripulante e a fuga/perseguição, documentando isso como decisão
explícita a revisitar no Pacote 10. Ao decodificar `aventureiro.p.bas` no Pacote 10, apareceu uma
inversão real: a lógica atual de `reacao_tripulante_apos_turno` (`combat.c`) tem quem foge e quem
ataca **trocados** em relação ao original. O usuário pediu fidelidade total a essa mecânica no
Pacote 10, então este pacote implementa a versão completa.

## Descoberta do Pacote 10 (ponto de partida)

Linha 6507 do original: `IF (ME<MP OR RND<.1) AND MI THEN GOTO 6800`. Traduzindo: o tripulante só
foge (`GOTO 6800`, "entra em pânico") se **for agressivo (`MI=1`) E** (sua energia `ME` for menor
que o custo da própria arma `MP`, **OU** 10% de sorteio aleatório). Se `MI=0` (não agressivo), essa
condição nunca é verdadeira — ele **sempre contra-ataca** (cai direto em 6510). Hoje `combat.c` faz
o oposto: não-agressivo foge, agressivo sempre contra-ataca.

## Entregáveis

- **Inverter a condição de fuga em `reacao_tripulante_apos_turno`:** tripulante agressivo decide
  entre fugir (baseado em energia própria vs. custo da arma, ou 10% de sorte) e contra-atacar;
  não-agressivo sempre contra-ataca. Isso exige rastrear a energia do tripulante (`ME`, linha 6002:
  `LET ME=INT(RND*150+100)` ao entrar na sala) — hoje `Celula`/`Tripulante` não têm esse campo,
  só `tripulante_vida_atual`. Adicionar `tripulante_energia_atual` em `Celula` (`types.h`).
- **Perseguição do tripulante ao jogador fugir (linhas 2100-2150):** ao fugir para a sala vizinha,
  se ela já tiver outro tripulante **ou** 50% de sorteio, o tripulante de quem o jogador fugiu
  aparece lá também (`GOSUB 6860` transfere o registro pra nova célula). Hoje `comando_fugir` só
  desloca o jogador, o tripulante nunca aparece na sala de destino.
- **Fuga do tripulante com trilha e opção de seguir (linhas 6800-6859):** ao entrar em pânico e
  fugir com sucesso, o tripulante percorre um caminho aleatório de células vazias (linha 6824-6830,
  sorteando direção até achar uma sala sem tripulante); o jogo pergunta "quer segui-lo (S/N)?"; se
  sim, o jogador percorre esse mesmo caminho sala por sala e retoma o combate na célula de chegada
  (linhas 6837-6852). Se não seguir, o tripulante simplesmente desaparece (comportamento atual).
- Atualizar os comentários/decisão em `combat.h`/`combat.c` que hoje dizem "versão simplificada,
  decisão do Pacote 0" — não são mais válidos depois deste pacote.

## Critério de aceite

Jogando manualmente: um tripulante agressivo com pouca energia foge em vez de atacar; um
não-agressivo nunca foge, sempre revida; ao fugir de uma sala com tripulante, ele pode reaparecer
na sala de destino; ao atacar um tripulante que foge, o jogo oferece perseguir e, seguindo, o
combate continua na sala onde ele parou.
