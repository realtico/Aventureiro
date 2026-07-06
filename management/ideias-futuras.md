# Ideias futuras (pós-Pacote 10)

> Não fazem parte do backlog de construção em [backlog/](backlog/) — são melhorias a considerar
> **depois** que o Pacote 10 (polimento) estiver validado e o jogo completo estiver jogável e fiel
> ao original. Cada uma provavelmente vira seu próprio pacote pequeno quando chegar a vez.

- **Labirinto maior** — grid além de 8x8. Já é quase de graça: `grid_size` em `data/config.json` (Pacote 1), `map.c` (Pacote 4) já generaliza para qualquer tamanho até `MAX_SALAS` (32). Só validar performance/legibilidade em grids bem maiores.
- **Novos personagens (tripulantes)** — entradas novas em `data/crew.json`. Estrutura já suporta; só respeitar `id_arma` válido e `MAX_TRIPULANTES` (32) em `types.h`.
- **Novas armas** — entradas novas em `data/weapons.json`. Mesma ideia; respeitar `MAX_ARMAS` (16).
- **Mais aleatoriedade nas armas** — hoje dano é uniforme em `[1, dano_maximo]` (decisão do Pacote 0: "valores razoáveis", sem tentar decifrar a fórmula ofuscada do original). Pensar em variar a distribuição (ex.: dado com viés, críticos, variância por arma) — é lógica nova em `combat.c`, não só dado.
- **Comando de revelar mapa conhecido** — mostrar as salas já visitadas (não o mapa inteiro). Contradiz a decisão do Pacote 0 de manter a UI 100% textual sem mapa visual — repensar essa decisão junto, ou oferecer como tecla extra opcional/debug em `ui.c`/`game.c` sem virar o padrão do jogo.
- **Perseguição multi-sala (fuga do jogador e do tripulante)** — o original tinha isso nos dois sentidos, e a versão em C hoje (Pacote 6b, `combat.c`) é simplificada (decisão do Pacote 0, a revisitar no Pacote 10 - ver nota abaixo):
  - Jogador foge (linha 2010/2120 do original): ao chegar na sala vizinha, se ela já tiver outro tripulante OU 50% de sorteio, o mesmo tripulante de quem você fugiu te segue até lá (`GOSUB 6860` transfere o registro do tripulante pra nova sala). Hoje `comando_fugir` só desloca o jogador; o tripulante nunca segue.
  - Tripulante foge (linhas 6800-6859, "inimigo foge"): ele anda por um caminho aleatório - a checagem em 6830 (`IF R$(X,Y,7)<>" " THEN GOTO 6824`) faz o loop continuar de onde parou, sem resetar a posição, então o caminho pode ter várias salas de comprimento até achar uma sala vazia. Depois ele pergunta "quer segui-lo (S/N)?"; se sim, o jogador percorre esse caminho salvo (`X$`) sala por sala e retoma o combate na sala de chegada. Hoje `reacao_tripulante_apos_turno` só remove o tripulante da sala (ele "desaparece", sem reaparecer em lugar nenhum e sem opção de seguir).
  - Nota: isso não é uma ideia nova de melhoria - é a decisão explícita do Pacote 0 ("Fuga/perseguição de inimigos: versão simplificada"), com o próprio `management/README.md` marcando que só deve ser revisitada no Pacote 10. Essa entrada existe só para não perder os detalhes de linha/lógica do original quando chegar a hora.

Guardar mais ideias aqui conforme surgirem durante a implementação dos pacotes atuais.
