CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2

OBJ = main.o tac.o c_frontend.o analyzer.o optimizer.o llm_agent.o verifier.o cost.o orchestrator.o dataset.o logger.o csv.o

all: optimizer

optimizer: $(OBJ)
	$(CC) $(CFLAGS) -o optimizer $(OBJ)

server: server

run-server: server
	./server

server: server.c tac.o c_frontend.o analyzer.o optimizer.o llm_agent.o verifier.o cost.o orchestrator.o dataset.o logger.o csv.o
	$(CC) $(CFLAGS) -o server server.c tac.o c_frontend.o analyzer.o optimizer.o llm_agent.o verifier.o cost.o orchestrator.o dataset.o logger.o csv.o

dataset-generator: dataset_generator.o dataset.o tac.o
	$(CC) $(CFLAGS) -o dataset_generator dataset_generator.o dataset.o tac.o

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o optimizer server dataset_generator

run: optimizer
	./optimizer

demo: optimizer
	./optimizer --demo

.PHONY: all clean run demo server run-server dataset-generator
