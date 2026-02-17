set -e

gcc -Wall -Wextra -O2 -o hw5-asm assembler.c
gcc -Wall -Wextra -O2 -o hw5-sim simulator.c