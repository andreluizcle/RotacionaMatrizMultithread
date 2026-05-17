/**
 * rotacionaMat.c
 *
 * Projeto Prático - TT304 Sistemas Operacionais
 * Rotação de Matriz N x N em 90° horário usando POSIX Threads.
 *
 * Estratégia de rotação in-place (sem matriz auxiliar):
 *   Passo 1 — Transposta do triângulo superior:
 *             Cada thread processa linhas intercaladas (0 e N-1,
 *             1 e N-2, ...) para balancear a carga entre threads,
 *             já que o triângulo superior tem menos elementos
 *             nas linhas mais baixas.
 *   [BARREIRA] — garante que todas as threads concluíram a
 *             transposta antes de qualquer uma iniciar o passo 2.
 *   Passo 2 — Espelhamento horizontal de cada linha:
 *             Cada thread espelha suas linhas atribuídas,
 *             completando a rotação 90° horária.
 *
 * Otimizações aplicadas:
 *  - Rotação in-place: elimina a necessidade de matriz auxiliar.
 *  - Distribuição intercalada de linhas para balanceamento de carga.
 *  - Variáveis de muito acesso armazenadas em registradores para acesso rápido.
 *  - Alocação dinâmica em única etapa (malloc contíguo).
 *  - Trabalhar sempre no maximo com blocos de 16 
 *
 * Compilação:
 *   make   (use o Makefile fornecido)
 *
 * Uso:
 *   ./rotacionaMat <N> <T> <ArqEntrada> <ArqSaida>
 *
 * Exemplo:
 *   ./rotacionaMat 1000 4 matriz.dat matriz.rot
 */

/* Necessário para habilitar pthread_barrier_t no padrão POSIX */
#define _XOPEN_SOURCE 600

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <string.h>
#include <unistd.h>


/** Tamanho do bloco de cache tile (linhas e colunas) -> Ser . */
#define TILE 16

/* =========================================================
 * Estruturas
 * ========================================================= */

/**
 * Argumentos passados a cada thread de rotação.
 * Contém todos os dados necessários para os dois passos
 * (transposta e espelhamento) sem variáveis globais.
 */
typedef struct {
    int               id;        /**< Identificador da thread (0-based).        */
    int               N;         /**< Dimensão da matriz (N x N).               */
    int               T;         /**< Total de threads.                          */
    int             **mat;       /**< Matriz a ser rotacionada in-place.         */
    pthread_barrier_t *barreira;       /**< Barreira entre transposta e espelhamento.  */
    double            tempo;           /**< Tempo total da thread.                     */
    double            tempoTransposta; /**< Tempo gasto apenas na transposta.          */
    double            tempoEspelho;    /**< Tempo gasto apenas no espelhamento.        */
} ArgThread;

/* =========================================================
 * Funções auxiliares: alocação de matriz
 * ========================================================= */

/**
 * Aloca uma matriz N x N de inteiros em um único bloco contíguo
 * de memória, retornando um vetor de ponteiros para cada linha.
 *
 * A alocação em uma única etapa (um único malloc para os dados)
 * satisfaz o requisito do projeto e melhora a localidade de cache.
 *
 * @param N  Dimensão da matriz.
 * @return   Ponteiro para o vetor de ponteiros de linhas, ou NULL em erro.
 */
static int **alocaMatriz(int N)
{
    /* Vetor de ponteiros de linha */
    int **mat = (int **)malloc((size_t)N * sizeof(int *));
    if (mat == NULL) {
        return NULL;
    }

    /* Bloco único contíguo para todos os elementos */
    int *bloco = (int *)malloc((size_t)N * (size_t)N * sizeof(int));
    if (bloco == NULL) {
        free(mat);
        return NULL;
    }

    /* Aponta cada linha para a posição correta dentro do bloco */
    register int i;
    for (i = 0; i < N; i++) {
        mat[i] = bloco + (size_t)i * N;
    }

    return mat;
}

/**
 * Libera a matriz alocada com alocaMatriz().
 *
 * @param mat  Vetor de ponteiros de linhas retornado por alocaMatriz().
 */
static void liberaMatriz(int **mat)
{
    if (mat == NULL) return;
    free(mat[0]); /* libera o bloco contíguo de dados */
    free(mat);    /* libera o vetor de ponteiros       */
}

/* =========================================================
 * Funções auxiliares: E/S de arquivo
 * ========================================================= */

/**
 * Lê a matriz N x N do arquivo de entrada (formato texto, linha a linha).
 *
 * @param nomeArq  Caminho do arquivo de entrada.
 * @param mat      Matriz já alocada onde os valores serão armazenados.
 * @param N        Dimensão da matriz.
 * @return         0 em sucesso, 1 em erro.
 */
static int leMatriz(const char *nomeArq, int **mat, int N)
{
    FILE *arq = fopen(nomeArq, "r");
    if (arq == NULL) {
        fprintf(stderr, "Erro: nao foi possivel abrir '%s' para leitura.\n",
                nomeArq);
        return 1;
    }

    register int i, j;
    for (i = 0; i < N; i++) {
        for (j = 0; j < N; j++) {
            if (fscanf(arq, "%d", &mat[i][j]) != 1) {
                fprintf(stderr,
                        "Erro: leitura falhou na posicao [%d][%d].\n", i, j);
                fclose(arq);
                return 1;
            }
        }
    }

    fclose(arq);
    return 0;
}

/**
 * Grava a matriz N x N no arquivo de saída (mesmo formato do arquivo de entrada).
 *
 * @param nomeArq  Caminho do arquivo de saída.
 * @param mat      Matriz a ser gravada.
 * @param N        Dimensão da matriz.
 * @return         0 em sucesso, 1 em erro.
 */
static int gravaMatriz(const char *nomeArq, int **mat, int N)
{
    FILE *arq = fopen(nomeArq, "w");
    if (arq == NULL) {
        fprintf(stderr, "Erro: nao foi possivel abrir '%s' para escrita.\n",
                nomeArq);
        return 1;
    }

    register int i, j;
    for (i = 0; i < N; i++) {
        for (j = 0; j < N; j++) {
            if (j > 0) fputc(' ', arq);
            fprintf(arq, "%d", mat[i][j]);
        }
        fputc('\n', arq);
    }

    fclose(arq);
    return 0;
}

/* =========================================================
 * Passo 1: Transposta do triângulo superior com tiling
 * ========================================================= */

/**
 * Realiza a transposta in-place das linhas intercaladas atribuídas
 * a esta thread, processando apenas o triângulo superior (j > i)
 * para evitar que duas threads troquem o mesmo par de elementos.
 *
 * Distribuição intercalada:
 *   Thread id processa as linhas: id, id+T, id+2T, ...
 *   Para cada linha i, apenas posições j > i são processadas,
 *   garantindo que cada par (i,j) seja tocado por exatamente
 *   uma thread, sem necessidade de mutex.
 *
 * Tiling TILE x TILE garante que os blocos de leitura/escrita
 * caibam inteiramente em cache L1 antes de avançar.
 *
 * @param mat  Matriz a ser transposta in-place.
 * @param N    Dimensão da matriz.
 * @param id   Identificador da thread.
 * @param T    Total de threads.
 */
static void transpostaBloco(int **mat, int N, int id, int T)
{
    register int i,  j;   /* iteradores de linha e coluna      */
    register int jMax;    /* limite do bloco de colunas        */
    register int tmp;     /* variável temporária para a troca  */

    for (i = id; i < N; i += T) {
        for (jMax = i + 1; jMax < N; jMax += TILE) {
            register int jFim = jMax + TILE;
            if (jFim > N) jFim = N;

            for (j = jMax; j < jFim; j++) {
                tmp       = mat[i][j];
                mat[i][j] = mat[j][i];
                mat[j][i] = tmp;
            }
        }
    }

    /* Suprime warnings de variáveis não usadas nesta versão simplificada */
}

/* =========================================================
 * Passo 2: Espelhamento horizontal de linhas
 * ========================================================= */

/**
 * Espelha horizontalmente as linhas atribuídas a esta thread.
 * Cada linha i tem seus elementos trocados: mat[i][j] ↔ mat[i][N-1-j],
 * percorrendo apenas a metade esquerda (j < N/2).
 *
 * Distribuição: mesma intercalação do passo 1 (linhas id, id+T, id+2T, ...),
 * mantendo o balanceamento de carga uniforme entre as threads.
 *
 * @param mat  Matriz já transposta, a ser espelhada.
 * @param N    Dimensão da matriz.
 * @param id   Identificador da thread.
 * @param T    Total de threads.
 */
static void espelhaLinhas(int **mat, int N, int id, int T)
{
    register int i, j;   /* iteradores de linha e coluna      */
    register int tmp;    /* variável temporária para a troca  */
    register int meio;   /* metade da largura da linha        */

    meio = N / 2;

    for (i = id; i < N; i += T) {
        for (j = 0; j < meio; j++) {
            tmp              = mat[i][j];
            mat[i][j]        = mat[i][N - 1 - j];
            mat[i][N - 1 - j] = tmp;
        }
    }
}

/* =========================================================
 * Função executada por cada thread de rotação
 * ========================================================= */

/**
 * Ponto de entrada de cada thread de processamento.
 *
 * Executa os dois passos da rotação in-place:
 *   1. Transposta do triângulo superior (com tiling e intercalação).
 *   2. Barreira: aguarda todas as threads concluírem a transposta.
 *   3. Espelhamento horizontal das linhas atribuídas.
 *
 * Mede o tempo total da thread (passo 1 + barreira + passo 2).
 *
 * @param arg  Ponteiro para ArgThread com os dados da thread.
 * @return     NULL (padrão POSIX).
 */
static void *threadRotaciona(void *arg)
{
    ArgThread *dados = (ArgThread *)arg;

    struct timespec inicio, fimTransposta, fimEspelho;

    /* Passo 1: Transposta do triângulo superior com tiling */
    clock_gettime(CLOCK_MONOTONIC, &inicio);
    transpostaBloco(dados->mat, dados->N, dados->id, dados->T);
    clock_gettime(CLOCK_MONOTONIC, &fimTransposta);

    /* Barreira: todas as threads devem terminar a transposta
     * antes de qualquer uma iniciar o espelhamento           */
    pthread_barrier_wait(dados->barreira);

    /* Passo 2: Espelhamento horizontal das linhas */
    espelhaLinhas(dados->mat, dados->N, dados->id, dados->T);
    clock_gettime(CLOCK_MONOTONIC, &fimEspelho);

    dados->tempoTransposta = (double)(fimTransposta.tv_sec  - inicio.tv_sec)
                           + (double)(fimTransposta.tv_nsec - inicio.tv_nsec) / 1.0e9;

    dados->tempoEspelho    = (double)(fimEspelho.tv_sec  - fimTransposta.tv_sec)
                           + (double)(fimEspelho.tv_nsec - fimTransposta.tv_nsec) / 1.0e9;

    dados->tempo           = (double)(fimEspelho.tv_sec  - inicio.tv_sec)
                           + (double)(fimEspelho.tv_nsec - inicio.tv_nsec) / 1.0e9;

    return NULL;
}

/* =========================================================
 * Função de distribuição de trabalho entre threads
 * ========================================================= */

/**
 * Inicializa a barreira, cria T threads de processamento,
 * aguarda a conclusão e imprime os tempos de execução.
 *
 * @param mat  Matriz a ser rotacionada in-place.
 * @param N    Dimensão da matriz.
 * @param T    Número de threads.
 * @return     0 em sucesso, 1 em erro.
 */
static int executaThreads(int **mat, int N, int T)
{
    pthread_t         *threads  = (pthread_t *)malloc((size_t)T * sizeof(pthread_t));
    ArgThread         *args     = (ArgThread *)malloc((size_t)T * sizeof(ArgThread));
    pthread_barrier_t *barreira = (pthread_barrier_t *)malloc(sizeof(pthread_barrier_t));

    if (threads == NULL || args == NULL || barreira == NULL) {
        fprintf(stderr, "Erro: falha ao alocar estruturas de threads.\n");
        free(threads);
        free(args);
        free(barreira);
        return 1;
    }

    /* Inicializa a barreira para T threads */
    if (pthread_barrier_init(barreira, NULL, (unsigned int)T) != 0) {
        fprintf(stderr, "Erro: falha ao inicializar barreira.\n");
        free(threads);
        free(args);
        free(barreira);
        return 1;
    }

    struct timespec inicioTotal, fimTotal;
    clock_gettime(CLOCK_MONOTONIC, &inicioTotal);

    /* Cria as threads — cada uma usa seu id e T para
     * calcular suas próprias linhas intercaladas        */
    register int t;
    for (t = 0; t < T; t++) {
        args[t].id       = t;
        args[t].N        = N;
        args[t].T        = T;
        args[t].mat      = mat;
        args[t].barreira        = barreira;
        args[t].tempo           = 0.0;
        args[t].tempoTransposta = 0.0;
        args[t].tempoEspelho    = 0.0;

        if (pthread_create(&threads[t], NULL, threadRotaciona, &args[t]) != 0) {
            fprintf(stderr, "Erro: falha ao criar thread %d.\n", t);
            pthread_barrier_destroy(barreira);
            free(threads);
            free(args);
            free(barreira);
            return 1;
        }
    }

    /* Aguarda todas as threads concluírem */
    for (t = 0; t < T; t++) {
        pthread_join(threads[t], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &fimTotal);

    double tempoTotal = (double)(fimTotal.tv_sec  - inicioTotal.tv_sec)
                      + (double)(fimTotal.tv_nsec - inicioTotal.tv_nsec) / 1.0e9;

    /* Exibe tempos individuais e total (precisão de nanossegundos) */
    for (t = 0; t < T; t++) {
        printf("Tempo de execucao do Thread %d (transposta):    %.9f segundos.\n",
               args[t].id, args[t].tempoTransposta);
        printf("Tempo de execucao do Thread %d (espelhamento):  %.9f segundos.\n",
               args[t].id, args[t].tempoEspelho);
        printf("Tempo de execucao do Thread %d (total):         %.9f segundos.\n",
               args[t].id, args[t].tempo);
        printf("\n");
    }
    printf("Tempo total de execucao: %.9f segundos.\n", tempoTotal);

    pthread_barrier_destroy(barreira);
    free(threads);
    free(args);
    free(barreira);
    return 0;
}

/* =========================================================
 * Validação de argumentos
 * ========================================================= */

/**
 * Valida e converte os argumentos da linha de comando.
 * Emite avisos se T superar N ou os núcleos físicos disponíveis.
 *
 * @param argc       Número de argumentos.
 * @param argv       Vetor de argumentos.
 * @param outN       Saída: dimensão N.
 * @param outT       Saída: número de threads T.
 * @param outEntrada Saída: nome do arquivo de entrada.
 * @param outSaida   Saída: nome do arquivo de saída.
 * @return           0 em sucesso, 1 em erro.
 */
static int validaArgs(int argc, char *argv[],
                      int *outN, int *outT,
                      const char **outEntrada, const char **outSaida)
{
    if (argc != 5) {
        fprintf(stderr,
                "Uso: %s <N> <T> <ArqEntrada> <ArqSaida>\n"
                "  N          : dimensao da matriz (N x N)\n"
                "  T          : numero de threads\n"
                "  ArqEntrada : arquivo de entrada\n"
                "  ArqSaida   : arquivo de saida (.rot)\n",
                argv[0]);
        return 1;
    }

    *outN = atoi(argv[1]);
    *outT = atoi(argv[2]);

    if (*outN <= 0) {
        fprintf(stderr, "Erro: N deve ser um inteiro positivo.\n");
        return 1;
    }
    if (*outT <= 0) {
        fprintf(stderr, "Erro: T deve ser um inteiro positivo.\n");
        return 1;
    }

    /* Limita T ao tamanho da matriz */
    if (*outT > *outN) {
        fprintf(stderr,
                "Aviso: T (%d) maior que N (%d). Ajustando T = N.\n",
                *outT, *outN);
        *outT = *outN;
    }

    /* Avisa se T excede o número de núcleos físicos disponíveis */
    int nucleos = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (nucleos > 0 && *outT > nucleos) {
        fprintf(stderr,
                "Aviso: T (%d) maior que o numero de nucleos disponiveis (%d).\n"
                "         O programa continuara, mas pode haver queda de desempenho\n"
                "         devido ao escalonamento (context switch) do sistema operacional.\n",
                *outT, nucleos);
    }

    *outEntrada = argv[3];
    *outSaida   = argv[4];
    return 0;
}

/* =========================================================
 * main
 * ========================================================= */

int main(int argc, char *argv[])
{
    int         N, T;
    const char *arqEntrada, *arqSaida;

    /* 1. Valida argumentos da linha de comando */
    if (validaArgs(argc, argv, &N, &T, &arqEntrada, &arqSaida) != 0) {
        return 1;
    }

    printf("Configuracao: N=%d, T=%d, entrada='%s', saida='%s'\n",
           N, T, arqEntrada, arqSaida);

    /* 2. Aloca a matriz em um único bloco contíguo */
    int **mat = alocaMatriz(N);
    if (mat == NULL) {
        fprintf(stderr, "Erro: falha na alocacao da matriz.\n");
        return 1;
    }

    /* 3. Lê a matriz do arquivo (tempo de I/O não é contado) */
    if (leMatriz(arqEntrada, mat, N) != 0) {
        liberaMatriz(mat);
        return 1;
    }

    /* 4. Rotaciona in-place usando múltiplas threads:
     *    transposta intercalada + barreira + espelhamento  */
    if (executaThreads(mat, N, T) != 0) {
        liberaMatriz(mat);
        return 1;
    }

    /* 5. Grava a matriz rotacionada no arquivo de saída (I/O não é contado) */
    if (gravaMatriz(arqSaida, mat, N) != 0) {
        liberaMatriz(mat);
        return 1;
    }

    printf("Matriz rotacionada gravada em '%s'.\n", arqSaida);

    /* 6. Libera memória */
    liberaMatriz(mat);

    return 0;
}