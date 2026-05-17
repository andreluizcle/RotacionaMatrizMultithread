# ===========================================================
# Makefile
# Uso:
#   make            -> compila o programa
#   make geraMatriz -> compila o gerador de matriz de teste
#   make test       -> executa testes com 1, 2, 4 e 8 threads
#   make clean      -> remove binários e arquivos gerados
# ===========================================================

CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c99 -pthread
TARGET  = rotacionaMat
GERADOR = geraMatriz
N       = 1000

.PHONY: all geraMatriz test clean

all: $(TARGET)

$(TARGET): rotacionaMat.c
	$(CC) $(CFLAGS) -o $(TARGET) rotacionaMat.c

$(GERADOR): geraMatriz.c
	$(CC) $(CFLAGS) -o $(GERADOR) geraMatriz.c

# Gera arquivo de teste e roda com 1, 2, 4 e 8 threads
test: $(TARGET) $(GERADOR)
	@echo "=== Gerando matriz $(N)x$(N) ==="
	./$(GERADOR) $(N) matriz.dat
	@echo ""
	@echo "=== T=1 thread ==="
	./$(TARGET) $(N) 1 matriz.dat matriz_t1.rot
	@echo ""
	@echo "=== T=2 threads ==="
	./$(TARGET) $(N) 2 matriz.dat matriz_t2.rot
	@echo ""
	@echo "=== T=4 threads ==="
	./$(TARGET) $(N) 4 matriz.dat matriz_t4.rot
	@echo ""
	@echo "=== T=8 threads ==="
	./$(TARGET) $(N) 8 matriz.dat matriz_t8.rot

clean:
	rm -f $(TARGET) $(GERADOR) matriz.dat *.rot