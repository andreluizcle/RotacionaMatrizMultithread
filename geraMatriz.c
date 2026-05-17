/**
 * geraMatriz.c
 *
 * Utilitário auxiliar: gera um arquivo de matriz N x N com inteiros
 * aleatórios (positivos e negativos) para uso nos experimentos.
 *
 * Uso:
 *   ./geraMatriz <N> <ArqSaida>
 *
 * Exemplo:
 *   ./geraMatriz 1000 matriz.dat
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <N> <ArqSaida>\n", argv[0]);
        return 1;
    }

    int N = atoi(argv[1]);
    if (N <= 0) {
        fprintf(stderr, "Erro: N deve ser positivo.\n");
        return 1;
    }

    FILE *arq = fopen(argv[2], "w");
    if (arq == NULL) {
        fprintf(stderr, "Erro: nao foi possivel criar '%s'.\n", argv[2]);
        return 1;
    }

    srand((unsigned int)time(NULL));

    register int i, j;
    for (i = 0; i < N; i++) {
        for (j = 0; j < N; j++) {
            /* Gera inteiro entre -9999 e 9999 */
            int val = (rand() % 19999) - 9999;
            if (j > 0) fputc(' ', arq);
            fprintf(arq, "%d", val);
        }
        fputc('\n', arq);
    }

    fclose(arq);
    printf("Matriz %dx%d gerada em '%s'.\n", N, N, argv[2]);
    return 0;
}