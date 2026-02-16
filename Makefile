CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2

all: test

test:
	$(CC) $(CFLAGS) SimulatorTest.c -o test_runner
	./test_runner

clean:
	rm -f sim test_runner test_*.bin out.txt
