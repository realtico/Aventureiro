# Pacote 11 — Melhorias de jogabilidade

**Tamanho:** S · **Depende de:** [Pacote 10](10-polimento.md)

## Objetivo

Ajustes de usabilidade percebidos jogando a versão atual (`game.c`/`ui.c`), sem mudar regras nem
dados de balanceamento — só a forma como o jogo pede informação e narra o que já sabe.

## Entregáveis

- **Mover (comando 0):** hoje `comando_mover_interativo` (`game.c`) sempre pergunta "Para que lado
  voce quer se movimentar (N/S/L/O)?" e aceita as quatro teclas mesmo que a sala atual não tenha
  saída em alguma direção — só descobre isso depois, na mensagem de falha de `comando_mover`
  (`combat.c`, "Nao ha saida pelo ..."). Passar a olhar `Celula::conectada[]` da sala atual antes de
  perguntar, listar e aceitar só as direções com porta de fato (ex.: só há Sul e Leste → "Para que
  lado (S/L)?", e `ler_opcao` só aceita essas duas teclas).
- **Examinar a sala (comando 8):** hoje `comando_examinar_sala_interativo` vai direto pro resultado
  da busca (achou item ou não), sem repetir a descrição da sala. Repetir a mesma narração de tipo de
  sala/saídas/tripulante que `entrar_em_sala` (`combat.c`, linha 6002 do original) já produz ao
  mover — reaproveitar essa lógica em vez de duplicá-la, para o jogador não precisar sair e voltar
  na sala só para reler a descrição.
- **Tecla de ajuda (`h`/`H`):** `ui_ler_comando` hoje só aceita dígitos 0-9. Aceitar também `h`/`H`
  dentro do loop principal (`game_loop`) como pseudo-comando que reexibe a lista dos 10 comandos
  (a mesma lista da tela de título, sem repetir a história completa) e volta pro jogo sem consumir
  uma rodada de verdade (sem custo de energia, sem contar como turno para nada que dependa disso).
- **Limpar a tela a cada comando:** pedido do usuário depois de jogar a versão atual (o log rolante
  acumula a narração de todos os comandos, ficando poluído). Na verdade isso é mais fiel ao original
  do que o comportamento atual: a linha 600 do BASIC (`600 CLS` antes de `605 GOSUB 500+C*500`) já
  limpava a tela antes de despachar cada comando. Chamar `ui_limpar_log()` em `game_loop` logo após
  ler um comando válido (0-9), antes do `switch` — assim cada rodada começa numa tela limpa,
  incluindo os sub-prompts (troca de arma, comunicar-se, mover). A tela de ajuda (`h`/`H`) também
  limpa antes de mostrar a lista e espera uma tecla antes de voltar, para não sumir sozinha no
  próximo redesenho do HUD.

## Critério de aceite

Jogando manualmente num terminal real: em uma sala com só algumas saídas, o comando Mover oferece
e aceita só essas direções; o comando Examinar sempre mostra a descrição da sala antes/junto do
resultado da busca; apertar `h` a qualquer momento do loop principal mostra a lista de comandos,
espera uma tecla e retorna ao jogo sem alterar vida/energia/dinheiro/posição do jogador; a cada novo
comando digitado, a tela é limpa antes de mostrar a narração daquele comando (sem acumular texto de
comandos anteriores).
