# RotacionaMatAntigo

Projeto de estudos em **C** sobre programação com **POSIX Threads (pthreads)**, feito durante o estudo de multithreading / Sistemas Operacionais (baseado nos comentários do código, é um projeto prático da disciplina **TT304 - Sistemas Operacionais**).

O programa rotaciona uma matriz `N x N` em **90° no sentido horário**, dividindo o trabalho entre várias threads e comparando o tempo de execução com diferentes quantidades delas (1, 2, 4, 8...).

## O que o projeto faz

O núcleo do projeto é rotacionar uma matriz quadrada **in-place** (sem usar uma matriz auxiliar), o que é o principal desafio de concorrência aqui: várias threads escrevendo na mesma matriz sem se atrapalhar.

A rotação é feita em dois passos:

1. **Transposta do triângulo superior** — cada thread cuidada de um conjunto de linhas intercaladas (ex.: com 2 threads, uma pega as linhas 0, 2, 4... e a outra 1, 3, 5...) e troca `mat[i][j]` com `mat[j][i]` apenas para `j > i`. Isso garante que cada par de posições seja tocado por exatamente uma thread, sem precisar de mutex/lock.
2. **Barreira (`pthread_barrier_t`)** — todas as threads esperam aqui até que a transposta tenha terminado por completo, antes de qualquer uma seguir para o próximo passo.
3. **Espelhamento horizontal** — cada thread espelha as linhas que lhe foram atribuídas (`mat[i][j]` ↔ `mat[i][N-1-j]`), o que finaliza a rotação de 90°.

A distribuição intercalada de linhas (em vez de blocos contíguos) existe para balancear a carga: no triângulo superior, as primeiras linhas têm muito mais elementos que as últimas, então intercalar evita que uma thread fique com muito mais trabalho que outra.

O programa também mede e imprime o tempo de execução de cada thread (transposta, espelhamento e total), além do tempo total do programa, o que é o ponto principal do estudo: comparar o ganho de desempenho ao aumentar o número de threads.

## Arquivos do repositório

| Arquivo | O que é |
|---|---|
| `rotacionaMat.c` | Programa principal. Lê uma matriz de um arquivo, rotaciona 90° usando T threads e grava o resultado em outro arquivo. |
| `geraMatriz.c` | Utilitário auxiliar que gera um arquivo com uma matriz `N x N` de inteiros aleatórios, usado para criar os dados de teste. |
| `Makefile` | Automatiza a compilação dos dois programas e a execução dos testes com várias quantidades de threads. |

## Como compilar

É necessário ter o **GCC** e a biblioteca **pthreads** (padrão em qualquer Linux/macOS com ferramentas de desenvolvimento instaladas).

```bash
# compila o rotacionaMat
make

# compila o gerador de matriz de teste
make geraMatriz

# remove os binários e arquivos de matriz gerados
make clean
```

O `Makefile` usa as flags `-Wall -Wextra -O2 -std=c99 -pthread`.

## Como executar

### 1. Gerar uma matriz de teste

```bash
./geraMatriz <N> <ArquivoSaida>

# exemplo: matriz 1000x1000 com valores entre -9999 e 9999
./geraMatriz 1000 matriz.dat
```

### 2. Rotacionar a matriz

```bash
./rotacionaMat <N> <T> <ArquivoEntrada> <ArquivoSaida>

# exemplo: rotaciona a matriz 1000x1000 usando 4 threads
./rotacionaMat 1000 4 matriz.dat matriz.rot
```

- `N`: dimensão da matriz (deve bater com a matriz gerada).
- `T`: número de threads a usar.
- `ArquivoEntrada`: arquivo texto com a matriz original.
- `ArquivoSaida`: arquivo texto onde a matriz rotacionada será salva.

O programa avisa (mas continua rodando) se `T` for maior que `N` ou maior que o número de núcleos disponíveis na máquina.

### 3. Rodar o teste comparativo pronto

O Makefile já tem um alvo que gera uma matriz e roda a rotação com 1, 2, 4 e 8 threads em sequência, imprimindo os tempos de cada execução:

```bash
make test
```

Por padrão usa `N=1000`; para mudar o tamanho:

```bash
make test N=4000
```

## Formato do arquivo de matriz

Um arquivo texto simples: cada linha representa uma linha da matriz, com os valores separados por espaço. É o mesmo formato tanto para o arquivo gerado pelo `geraMatriz` quanto para o arquivo de saída do `rotacionaMat`.

## Observações

- Este é um projeto de estudo/prático (o nome "Antigo" sugere que é uma versão anterior de outro projeto do autor), então o foco está mais em explorar conceitos de threads (divisão de trabalho, sincronização com barreira, medição de tempo) do que em ser uma ferramenta de uso geral.
- O tempo de leitura e escrita dos arquivos **não** entra na medição — só o tempo da rotação em si é cronometrado, para isolar o efeito da paralelização.
