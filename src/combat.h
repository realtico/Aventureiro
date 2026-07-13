#ifndef AVENTUREIRO_COMBAT_H
#define AVENTUREIRO_COMBAT_H

#include <stdbool.h>

#include "types.h"

/*
 * "Entrar em sala nova" com tripulante presente (entrar_em_sala + narrar_sala
 * em combat.c) sozinho já usa 8 mensagens ("Você entrou numa nova sala." +
 * "Sala tipo" + "Saídas" + "Há alguém..." + "Há alguma coisa aqui..." +
 * "TWIN reporta..." + "É um ... armado com ..." + a fala do tripulante).
 * log_msg() descarta em silêncio qualquer mensagem além do limite (ver
 * abaixo) - com o valor antigo (8, dimensionado só para esse caso isolado),
 * qualquer comando que prefixe mensagens próprias antes de entrar_em_sala
 * já estourava e cortava a revelação do tripulante: comando_fugir quando o
 * tripulante persegue ("Você fugiu..." + "Infelizmente...veio atrás de
 * você." = +2, confirmado na prática) e combate_seguir_tripulante_fugido
 * (+1 mensagem "Você o segue..." por sala percorrida). 16 cobre o pior caso
 * real de comando_fugir com folga (11) e uma perseguição de até ~7 salas.
 * Uma trilha de fuga mais longa que isso (o limite teórico e' MAX_TRILHA_
 * FUGA=64) ainda cortaria a revelação final - log_msg() so' descarta o que
 * vier DEPOIS do limite, entao nao ha' como a revelação (a ultima mensagem
 * da sequencia) sobreviver a uma trilha grande o bastante sem aumentar o
 * buffer na mesma proporção. Aceito como residual: rarissimo num grid 8x8
 * (a trilha para no primeiro quarto vazio encontrado), e ja' era um limite
 * pre-existente antes deste pacote, so' que num limiar bem mais baixo.
 */
#define MAX_MENSAGENS_RESULTADO 16
#define MAX_TAMANHO_MENSAGEM 96

/* Tamanho maximo do caminho aleatorio que um tripulante em fuga percorre
 * ate achar uma sala sem outro tripulante (linha 6824-6830 do original,
 * Pacote 13) - e' uma salvaguarda nossa (o original nao tem limite
 * explicito), generosa o bastante pro grid 8x8 default; se estourar
 * (bem raro), o tripulante so desaparece, igual ao comportamento
 * simplificado anterior. */
#define MAX_TRILHA_FUGA 64

/*
 * Buffer de mensagens narrativas produzidas por um comando, no lugar de
 * imprimir direto na tela - isso e' responsabilidade da camada de UI
 * (Pacote 7), que ainda nao existe. 'sucesso' indica se a acao pretendida
 * de fato aconteceu (ex.: false se tentou mover para uma direcao sem
 * porta); 'jogador_morreu' sinaliza vida chegando a zero (por acidente no
 * escuro nesta fase - combate propriamente dito e' o Pacote 6b) para quem
 * orquestra o jogo (Pacote 8) decidir o que fazer. 'tripulante_fugiu' e
 * 'trilha_fuga'/'trilha_fuga_tamanho' (Pacote 13) sinalizam que um
 * tripulante fugiu com sucesso da sala nesta rodada (linha 6810 do
 * original) - quem orquestra o jogo deve perguntar "quer segui-lo (S/N)?"
 * e, se sim, chamar combate_seguir_tripulante_fugido com esse caminho.
 */
typedef struct {
    char mensagens[MAX_MENSAGENS_RESULTADO][MAX_TAMANHO_MENSAGEM];
    /* Pacote 20: pausa dramatica de ~1s apos esta mensagem, antes da
     * proxima - narrar() (game.c) checa isso entre cada ui_log(). Ponto
     * com lastro direto no original: o FOR/NEXT vazio antes de "VOCE
     * ENCONTROU..." (linha 5170-5180). Os demais pontos marcados
     * reconstroem a mesma sensacao onde o original nao tem um delay
     * proposital explicito (so' processamento de RAND USR genuinamente
     * lento no ZX81) - ver management/backlog/20-pausas-dramaticas.md. */
    bool pausa_apos[MAX_MENSAGENS_RESULTADO];
    int num_mensagens;
    bool sucesso;
    bool jogador_morreu;

    bool tripulante_fugiu;
    int trilha_fuga[MAX_TRILHA_FUGA];
    int trilha_fuga_tamanho;
} Resultado;

/*
 * Mover (linha 1004): anda na direcao indicada, se houver porta na celula
 * atual e nao houver tripulante vivo na sala bloqueando a saida (linha
 * 1004-1007 do original: "nao se arrisque dando as costas" ao tripulante -
 * resolver o encontro e' pre-requisito pra sair da sala). Ao entrar na
 * sala nova, narra tipo/saidas/tripulante (sub-rotina "entrar em sala
 * nova", linha 6002) e drena energia do escudo se estiver ligado (linha
 * 6115 + 3600).
 */
Resultado comando_mover(Jogador *jogador, Mapa *mapa, const BaseDeDados *bd, Direcao direcao);

/*
 * Trocar de arma (linha 2502): troca para a arma de indice
 * 'indice_no_inventario' dentro do inventario do jogador. Falha (sucesso
 * = false) se so houver uma arma (linha 2505: "voce so tem uma arma") ou
 * se o indice for invalido. O contra-ataque do tripulante apos trocar de
 * arma na presenca de um inimigo (linhas 2655-2670 do original) fica para
 * o Pacote 6b/8 ligarem, ja que depende de combate.
 */
Resultado comando_trocar_arma(Jogador *jogador, const BaseDeDados *bd, int indice_no_inventario);

/*
 * Escudo (linha 3501): liga/desliga o escudo. Falha se a energia estiver
 * zerada ou negativa (linha 3502: "energia insuficiente").
 */
Resultado comando_escudo(Jogador *jogador);

/*
 * Usar medicamento (linha 4010): wrapper sobre jogador_usar_medicamento
 * (Pacote 5/12) que acrescenta a narracao (sem medicamento / vida ja cheia /
 * curou). Curar com sucesso conta como uma rodada plena (linha 4100:
 * "GOTO 6500") - se houver tripulante vivo na sala, ele reage exatamente
 * como apos um ataque ou fuga, por isso a assinatura recebe 'mapa'/'bd'.
 */
Resultado comando_usar_medicamento(Jogador *jogador, Mapa *mapa, const BaseDeDados *bd, const Config *cfg);

/*
 * Situacao (linha 4510): relatorio somente-leitura de vida, medicamento,
 * energia, dinheiro, armas e arma atual.
 */
Resultado comando_situacao(const Jogador *jogador, const BaseDeDados *bd);

/*
 * Descreve tipo de sala/saidas/tripulante da sala atual do jogador, sem
 * narrar "entrada" nem drenar escudo - e' o mesmo conteudo que "entrar em
 * sala nova" (linha 6002) produz ao mover, exposto aqui para o comando
 * Examinar (Pacote 11) reaproveitar em vez de o jogador precisar sair e
 * voltar na sala so pra reler a descricao.
 */
void combat_narrar_sala_atual(const Jogador *jogador, const Mapa *mapa, const BaseDeDados *bd, Resultado *r);

/*
 * Examinar a sala (linha 5001): falha se houver tripulante vivo na sala
 * (linha 5002). Em sala clara, examina sem risco nem custo (linha
 * 5140-5150). Em sala escura, 'usar_lanterna' decide o caminho: com
 * lanterna gasta energia (linha 5210, CUSTO_ENERGIA_LANTERNA) sem risco;
 * sem lanterna arrisca acidente (linha 7000, Config::chance_acidente_no_escuro).
 * Se encontrar item nao coletado, sorteia o tipo (energia/medicamento/
 * dinheiro - o mapa (Pacote 4) simplificou o item para um bool, sem tipo
 * pre-definido por sala) e aplica o ganho.
 */
Resultado comando_examinar_sala(Jogador *jogador, Mapa *mapa, const Config *cfg, bool usar_lanterna);

/*
 * Acionar teleporte (linha 5510): so funciona parado na Sala de Teleporte
 * e sem tripulante vivo na sala (esta segunda checagem nunca deveria
 * disparar de fato, pois o gerador de mapa - Pacote 4 - garante que a Sala
 * de Teleporte nasce sempre "segura"; mantida por fidelidade/defesa).
 * sucesso = true significa que o jogador venceu a partida.
 */
Resultado comando_acionar_teleporte(Jogador *jogador, const Mapa *mapa);

/*
 * Atacar (linha 1505): ataca o tripulante da sala atual com a arma em uso.
 * Falha (sucesso = false) sem ninguem na sala ou sem energia para a arma.
 * Ao matar, sorteia loot (arma do tripulante, energia, e - so se
 * agressivo, linha 1800 - chance de medicamento/dinheiro). Se o
 * tripulante sobreviver ao ataque (errou ou nao matou), reage (linha
 * 6505/6507, Pacote 13): so tripulantes agressivos avaliam fugir (energia
 * propria baixa ou 10% de sorte); nao-agressivos sempre contra-atacam.
 */
Resultado comando_atacar(Jogador *jogador, Mapa *mapa, const BaseDeDados *bd);

/*
 * Fugir (linha 2010): falha sem tripulante na sala. 10% de chance de cair
 * na tentativa (linha 2040), o que da uma reacao gratis ao tripulante.
 * Tendo sucesso, foge para uma sala vizinha aleatoria entre as conectadas;
 * o tripulante de quem fugiu pode vir atras (linha 2120, Pacote 13): 50%
 * de chance, e so se a sala de destino ainda nao tiver outro tripulante.
 */
Resultado comando_fugir(Jogador *jogador, Mapa *mapa, const BaseDeDados *bd);

/* As tres posturas de "Comunicar-se" (linha 3010), escolhidas pelo chamador
 * ja que este pacote nao faz I/O interativo. */
typedef enum {
    COMUNICAR_SUBORNAR,
    COMUNICAR_IRRITAR,
    COMUNICAR_AMIGAVEL
} AcaoComunicar;

/*
 * Comunicar-se (linha 3010): falha sem tripulante na sala, ou se ele nao
 * for agressivo (linha 3040 - so tripulantes "inteligentes" conversam;
 * robots/serventes nao). 'valor_oferecido' so importa para
 * COMUNICAR_SUBORNAR (quanto dinheiro oferecer). Cada postura tem sua
 * propria chance de sucesso (linhas 3130-3410); fracasso aciona a reacao
 * do tripulante, igual ao ataque.
 */
Resultado comando_comunicar(Jogador *jogador, Mapa *mapa, const BaseDeDados *bd, AcaoComunicar acao, int valor_oferecido);

/*
 * Persegue um tripulante que fugiu com sucesso (linhas 6837-6852 do
 * original, Pacote 13) - so deve ser chamada quando um Resultado anterior
 * veio com 'tripulante_fugiu' true, passando 'trilha_fuga'/
 * 'trilha_fuga_tamanho' dele. O tripulante ja foi relocado pra sala de
 * destino independente do jogador seguir (isso acontece dentro do proprio
 * combate ao fugir); aqui so' se narra o jogador andando pelo mesmo
 * caminho, com 10% de chance por passo (a partir do segundo) de perder o
 * rastro (linha 6839) - nesse caso o jogador para onde estava e so' narra
 * a sala atual. Se completar o caminho, chega na sala do tripulante e o
 * combate continua la.
 */
Resultado combate_seguir_tripulante_fugido(Jogador *jogador, Mapa *mapa, const BaseDeDados *bd, const int *trilha_fuga, int trilha_fuga_tamanho);

#endif
