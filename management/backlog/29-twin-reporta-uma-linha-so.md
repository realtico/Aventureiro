# Pacote 29 — bug: revelação do TWIN pode truncar/comer caracteres numa linha só

**Tamanho:** S · **Depende de:** [Pacote 15](15-narrar-arma-tripulante.md), [Pacote 20](20-pausas-dramaticas.md)

## Objetivo

Usuário jogando notou: ao encontrar um tripulante, a fala final do TWIN ("É um X, armado com Y.
Frase.") às vezes sai com caracteres comidos/cortados. Pediu pra quebrar em linhas separadas em vez
de uma linha só.

Em `narrar_sala` ([combat.c:166-180](../../aventureiro/src/combat.c#L166-L180)), as duas primeiras
falas já são mensagens separadas com pausa entre elas (linhas 175-178, "Há alguém..." / "Há alguma
coisa aqui..."), conforme o comentário em [combat.c:171-174](../../aventureiro/src/combat.c#L171-L174)
já documenta a intenção de "3 falas espaçadas por pausa". Mas a terceira fala continua sendo **uma
única** chamada de `log_msg` que concatena três partes lógicas distintas (o rótulo "TWIN reporta...",
o nome+arma do tripulante, e a frase de efeito) numa string só:

```c
log_msg(r, "TWIN reporta... \"É um %s, armado com %s. %s.\"", tripulante->nome, arma->nome,
        tripulante->frase);
```

Essa decisão de manter tudo numa linha só foi deliberada no [Pacote 15](15-narrar-arma-tripulante.md)
("Não precisa reproduzir literalmente o 'TWIN REPORTA...' como linha separada — mensagem de log
única já basta") — mas isso foi decidido **antes** de qualquer checagem de tamanho de buffer, e o
buffer é pequeno: `log_msg` (`combat.c:97-106`) escreve com `vsnprintf(..., MAX_TAMANHO_MENSAGEM,
...)`, e `MAX_TAMANHO_MENSAGEM` é só **96 bytes** ([combat.h:9](../../aventureiro/src/combat.h#L9)).
`vsnprintf` trunca silenciosamente o que não couber — sem erro, sem aviso, só corta o fim da string.

Conferi contra os dados reais (`data/crew.json` + `data/weapons.json`, script Python ad-hoc somando
os bytes UTF-8 da mensagem combinada pra cada um dos 20 tripulantes com sua arma associada
(`id_arma`)): **3 dos 20** já excedem 96 bytes hoje. O pior caso é "Médico Buteriano" com "Bisturi
Laser": a mensagem montada tem **100 bytes**, 4 acima do limite — `vsnprintf` corta os últimos
caracteres (a mensagem seria `"TWIN reporta... "É um Médico Buteriano, armado com Bisturi Laser.
Inteligente mas pouco perigoso."` mas sai cortada antes do fim). Isso bate exatamente com "come
caracteres": não é (só) um problema de quebra de linha visual no `janela_log` do ncurses, é um
truncamento real na hora de montar a string, antes mesmo de chegar na tela.

## Entregáveis

- Quebrar a chamada de `combat.c:179-180` em 3 chamadas separadas de `log_msg` + `marcar_pausa`,
  no mesmo padrão já usado nas duas falas anteriores (linhas 175-178):
  ```c
  log_msg(r, "TWIN reporta...");
  marcar_pausa(r);
  log_msg(r, "É um %s, armado com %s.", tripulante->nome, arma->nome);
  marcar_pausa(r);
  log_msg(r, "%s", tripulante->frase);
  ```
  Cada pedaço fica bem abaixo de 96 bytes mesmo no pior caso real (linha "É um .../armado com..."
  mais longa encontrada nos dados: "Oficial de Comunicações" + "Revólver Sônico" = 62 bytes; frase
  mais longa: "Inteligente mas pouco perigoso" = 30 bytes) — resolve o truncamento sem precisar
  aumentar `MAX_TAMANHO_MENSAGEM`.
- Atualizar o comentário em `combat.c:171-174` se necessário (ele já fala em "3 falas com pausa" —
  conferir se ainda descreve bem o fluxo depois da mudança, já que agora são 4 falas nessa parte,
  mais as 2 de sala tipo/saídas antes).
- Conferir `MAX_MENSAGENS_RESULTADO` (`combat.h:8`, hoje 8): o encontro com tripulante já usa "Sala
  tipo" + "Saídas" + as agora 5 falas do tripulante = 7 mensagens no mesmo `Resultado` — cabe, mas
  por pouca margem (`log_msg` vira no-op silencioso se `num_mensagens >= MAX_MENSAGENS_RESULTADO`,
  sem avisar). Vale um teste manual/smoke test específico garantindo que nenhuma fala desaparece
  nesse cenário mais carregado.
- `tests/smoke_test.py`: adicionar caso cobrindo especificamente o pior caso encontrado (tripulante
  "Médico Buteriano"/arma "Bisturi Laser", ou o par que acabar sendo o pior depois de qualquer edição
  futura em `data/crew.json`/`data/weapons.json`) confirmando que a frase completa aparece sem corte.

## Critério de aceite

Jogando manualmente até encontrar o tripulante "Médico Buteriano" (ou outro dos 3 identificados como
excedendo 96 bytes hoje): as três informações — "TWIN reporta...", "É um Médico Buteriano, armado com
Bisturi Laser.", "Inteligente mas pouco perigoso." — aparecem em linhas separadas, completas, sem
nenhum caractere cortado no meio. Encontros com os demais 17 tripulantes continuam funcionando
normalmente. `tests/smoke_test.py` continua passando.

**Resolvido e confirmado.** `narrar_sala` ([combat.c:171-188](../../aventureiro/src/combat.c#L171-L188))
quebrada em 5 `log_msg` (rótulo / nome+arma / frase, além das 2 falas anteriores já existentes), mas
**sem** `marcar_pausa` entre as 3 novas — ajuste de ritmo pedido pelo usuário depois da primeira
versão: as pausas dramáticas continuam só entre "Há alguém..." e "Há alguma coisa aqui..." (constroem
a tensão), mas a revelação em si (rótulo + nome/arma + frase) sai toda de uma vez, sem pausa extra
entre as 3 partes — a tensão já foi construída, e mais pausa ali só deixaria a revelação lenta sem
ganho de ritmo. Comentário acima atualizado pra explicar essa distinção (não fala mais em "3 falas
com pausa" genérico). `MAX_MENSAGENS_RESULTADO` (8) conferido: o encontro com tripulante usa 7
mensagens no mesmo `Resultado` (Sala tipo, Saídas, + 5 falas do tripulante) — cabe, sem estourar.

Achar um cenário reproduzível pro pior caso ("Médico Buteriano"/"Bisturi Laser", 100 bytes
combinados) sem depender de sorte do fuzzing: escrito um programa auxiliar descartável (fora do
repositório, reaproveitando `map.c`/`data_loader.c` reais) que roda `gerar_mapa()` para várias
sementes e faz BFS a partir da Sala de Teleporte até achar a sala com `id_tripulante == 11`. Achado:
**seed 33**, tripulante a exatamente 1 porta (Norte) do início. `tests/smoke_test.py`: nova
`verificar_fala_tripulante_nao_trunca` usa esse seed, manda a seta Norte, e confirma que as 3 linhas
aparecem completas (`"TWIN reporta..."`, `"É um Médico Buteriano, armado com Bisturi Laser."`,
`"Inteligente mas pouco perigoso"`).

Verificação: rodado manualmente com pexpect+pyte (seed 33, seta Norte) — as 3 linhas aparecem
completas, sem corte, confirmando que o bug de truncamento (comprovado antes com o script Python
somando bytes) está corrigido. `ctest`/`smoke_test.py` completo (painel de mapa, barra de comandos,
fala do tripulante, setas, fuzz) rodado 3x seguidas sem falha — e mais 2x depois do ajuste de ritmo
(sem pausa entre as 3 falas da revelação), também sem falha.
