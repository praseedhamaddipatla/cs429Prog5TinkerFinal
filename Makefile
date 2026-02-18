# Compiler
CC=gcc
CFLAGS=-Wall -Wextra -O2

# Binaries
ASM=hw5-asm
SIM=hw5-sim

# Test harness
TEST_BIN=tinker-test
TEST_SRC=TinkerTest.c

# Tinker programs
TK_FILES=fibonacci.tk binary_search.tk matrix_multiplication.tk
TKO_FILES=fibonacci.tko binary_search.tko matrix_multiplication.tko

# Default target
all: build assemble test-build test

# ==========================================
# Build assembler and simulator FIRST
# ==========================================

build:
	./build.sh

# ==========================================
# Assemble with hexdump verification
# ==========================================

assemble: build $(TKO_FILES)

fibonacci.tko: fibonacci.tk
	./$(ASM) fibonacci.tk fibonacci.tko
	@echo "========== HEXDUMP: fibonacci.tko =========="
	hexdump -C fibonacci.tko | head -n 40
	@echo

binary_search.tko: binary_search.tk
	./$(ASM) binary_search.tk binary_search.tko
	@echo "========== HEXDUMP: binary_search.tko =========="
	hexdump -C binary_search.tko | head -n 40
	@echo

matrix_multiplication.tko: matrix_multiplication.tk
	./$(ASM) matrix_multiplication.tk matrix_multiplication.tko
	@echo "========== HEXDUMP: matrix_multiplication.tko =========="
	hexdump -C matrix_multiplication.tko | head -n 40
	@echo

# ==========================================
# Build test harness
# ==========================================

test-build:
	$(CC) $(CFLAGS) -o $(TEST_BIN) $(TEST_SRC)

# ==========================================
# Run tests
# ==========================================

test:
	./$(TEST_BIN)

# ==========================================
# Run programs manually
# ==========================================

run-fibonacci: assemble
	./$(SIM) fibonacci.tko

binary_input.txt:
	echo "5\n1\n3\n5\n7\n9\n7" > binary_input.txt

run-binary: assemble binary_input.txt
	./$(SIM) binary_search.tko < binary_input.txt

run-matrix: assemble
	./$(SIM) matrix_multiplication.tko

run-all: run-fibonacci run-binary run-matrix

# ==========================================
# Clean
# ==========================================

clean:
	rm -f $(ASM) $(SIM)
	rm -f $(TKO_FILES)
	rm -f $(TEST_BIN)
	rm -f tmp_input.txt tmp_output.txt

# ==========================================
# Rebuild everything
# ==========================================

rebuild: clean all
