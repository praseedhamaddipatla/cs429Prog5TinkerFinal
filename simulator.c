#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEM_SIZE (1 << 19)
#define REG 32
#define INC 4

// tinker file header
typedef struct {
    uint64_t file_type;
    uint64_t code_seg_begin; // code segment
    uint64_t code_seg_size;
    uint64_t data_seg_begin; // data segment
    uint64_t data_seg_size;
} TinkerFileHeader;

static uint64_t pc;
static int running;
static uint64_t regs[REG];
static uint8_t mem[MEM_SIZE];

// initialization
void initMachine(void) {
    memset(mem, 0, sizeof(mem));
    memset(regs, 0, sizeof(regs));
    regs[31] = MEM_SIZE;
    running = 1;
    // pc will be set after reading the header
}

// helpers
static uint32_t getOpcode(uint32_t i) { return (i >> 27) & 0x1F; }
static uint32_t getrd(uint32_t i) { return (i >> 22) & 0x1F; }
static uint32_t getrs(uint32_t i) { return (i >> 17) & 0x1F; }
static uint32_t getrt(uint32_t i) { return (i >> 12) & 0x1F; }
static inline uint64_t getImm(uint32_t instr) { return instr & 0xFFF; }

static int32_t getL(uint32_t i) {
    int32_t imm = i & 0xFFF;
    if (imm & 0x800)
        imm |= ~0xFFF;
    return imm;
}

// memory helpers
uint64_t load64(uint64_t addr) {
    if (addr > MEM_SIZE - 8) {
        fprintf(stderr, "Simulation error\n");
        exit(1);
    }

    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v |= ((uint64_t)mem[addr + i]) << (8 * i);
    return v;
}

void store64(uint64_t addr, uint64_t val) {
    if (addr % 8 != 0 || addr + 7 >= MEM_SIZE) {
        fprintf(stderr, "Simulation error\n");
        exit(1);
    }

    for (int i = 0; i < 8; i++)
        mem[addr + i] = (val >> (8 * i)) & 0xFF;
}

// fetch
uint32_t fetchInstr(void) {
    if (pc + 3 >= MEM_SIZE) {
        fprintf(stderr, "Simulation error \n");
        exit(1);
    }

    uint32_t instr = mem[pc] | (mem[pc + 1] << 8) | (mem[pc + 2] << 16) |
                     (mem[pc + 3] << 24);


    return instr;
}

// execution helpers
#define NEXT pc += INC

// logic
void execAnd(uint32_t i) {
    regs[getrd(i)] = regs[getrs(i)] & regs[getrt(i)];
    NEXT;
}
void execOr(uint32_t i) {
    regs[getrd(i)] = regs[getrs(i)] | regs[getrt(i)];
    NEXT;
}
void execXor(uint32_t i) {
    regs[getrd(i)] = regs[getrs(i)] ^ regs[getrt(i)];
    NEXT;
}
void execNot(uint32_t i) {
    regs[getrd(i)] = ~regs[getrs(i)];
    NEXT;
}

// shifts
void execShftr(uint32_t i) {
    regs[getrd(i)] = regs[getrs(i)] >> regs[getrt(i)];
    NEXT;
}
void execShftri(uint32_t i) {
    regs[getrd(i)] >>= getL(i);
    NEXT;
}
void execShftl(uint32_t i) {
    regs[getrd(i)] = regs[getrs(i)] << regs[getrt(i)];
    NEXT;
}
void execShftli(uint32_t i) {
    regs[getrd(i)] <<= getL(i);
    NEXT;
}

// arithmetic
void execAdd(uint32_t i) {
    regs[getrd(i)] = regs[getrs(i)] + regs[getrt(i)];
    NEXT;
}
void execAddi(uint32_t i) {
    regs[getrd(i)] += getL(i);
    NEXT;
}
void execSub(uint32_t i) {
    regs[getrd(i)] = regs[getrs(i)] - regs[getrt(i)];
    NEXT;
}
void execSubi(uint32_t i) {
    regs[getrd(i)] -= getL(i);
    NEXT;
}
void execMul(uint32_t i) {
    regs[getrd(i)] = regs[getrs(i)] * regs[getrt(i)];
    NEXT;
}
void execDiv(uint32_t i) {
    if (!regs[getrt(i)]) {
        fprintf(stderr, "Simulation error\n");
        exit(1);
    }
    regs[getrd(i)] = (int64_t)regs[getrs(i)] / (int64_t)regs[getrt(i)];
    NEXT;
}

// mov
void execMovLoad(uint32_t i) {
    uint64_t addr = regs[getrs(i)] + getL(i);
    regs[getrd(i)] = load64(addr);
    NEXT;
}

void execMovStore(uint32_t i) {
    uint64_t addr = regs[getrd(i)] + getL(i);
    store64(addr, regs[getrs(i)]);
    NEXT;
}

void execMovReg(uint32_t i) {
    regs[getrd(i)] = regs[getrs(i)];
    NEXT;
}

void execMovImm(uint32_t i) {
    // keep upper 52 bits, set lower 12 bits to L
    uint32_t rd = getrd(i);
    uint64_t L = getImm(i); // unsigned
    regs[rd] = (regs[rd] & ~0xFFFULL) | L;
    NEXT;
}

// control
void execBrgt(uint32_t instr) {
    uint32_t rd = getrd(instr);
    uint32_t rs = getrs(instr);
    uint32_t rt = getrt(instr);
    int64_t v1 = (int64_t)regs[rs];
    int64_t v2 = (int64_t)regs[rt];
    if (v1 > v2) {
        pc = regs[rd];
    } else {
        pc = pc + INC;
    }
}

// priv
void execPriv(uint32_t i) {
    uint32_t L = getImm(i);

    switch (L) {
    case 0x0: // halt
        exit(0);

    case 0x3: { // input
        uint32_t rd = getrd(i);
        uint32_t rs = getrs(i);
        uint64_t p = regs[rs];

        if (p == 0 || p == 2) {
            char buf[256];
            if (!fgets(buf, sizeof(buf), stdin)) {
                fprintf(stderr, "Simulation error\n");
                exit(1);
            }

            char *end;
            errno = 0;
            unsigned long long val = strtoull(buf, &end, 10);

            if (errno == ERANGE || end == buf) {
                fprintf(stderr, "Simulation error\n");
                exit(1);
            }

            // bad negatives
            if (buf[0] == '-') {
                fprintf(stderr, "Simulation error\n");
                exit(1);
            }

            // trailing junk
            while (*end == ' ' || *end == '\t')
                end++;
            if (*end != '\n' && *end != '\0') {
                fprintf(stderr, "Simulation error\n");
                exit(1);
            }

            regs[rd] = val;
        } else {
            fprintf(stderr, "Simulation error\n");
            exit(1);
        }

        pc += INC;
        return;
    }

    case 0x4: { // output
        uint32_t rd = getrd(i);
        uint32_t rs = getrs(i);
        uint64_t p = regs[rd];
        if (p == 1 || p == 2) {
            // port 1 and port 2: print unsigned integer
            // IEEE 754 double bits printed as uint64 for port 2
            printf("%lu\n", (long unsigned int)regs[rs]);
        } else if (p == 3) {
            // port 3: print single ASCII character
            uint64_t ch = regs[rs];
            putchar((int)(ch & 0xFF));
        }
        pc = pc + INC;
        return;
    }

    default:
        fprintf(stderr, "Simulation error\n");
        exit(1);
    }
}

// floating point
void execAddf(uint32_t i) {
    double a, b, c;
    memcpy(&a, &regs[getrs(i)], 8);
    memcpy(&b, &regs[getrt(i)], 8);
    c = a + b;
    memcpy(&regs[getrd(i)], &c, 8);
    pc += INC;
}

void execSubf(uint32_t i) {
    double a, b, c;
    memcpy(&a, &regs[getrs(i)], 8);
    memcpy(&b, &regs[getrt(i)], 8);
    c = a - b;
    memcpy(&regs[getrd(i)], &c, 8);
    pc += INC;
}

void execMulf(uint32_t i) {
    double a, b, c;
    memcpy(&a, &regs[getrs(i)], 8);
    memcpy(&b, &regs[getrt(i)], 8);
    c = a * b;
    memcpy(&regs[getrd(i)], &c, 8);
    pc += INC;
}

void execDivf(uint32_t i) {
    double a, b, c;
    memcpy(&a, &regs[getrs(i)], 8);
    memcpy(&b, &regs[getrt(i)], 8);
    if (b == 0.0) {
        fprintf(stderr, "Simulation error\n");
        exit(1);
    }
    c = a / b;
    memcpy(&regs[getrd(i)], &c, 8);
    pc += INC;
}

// branch

void execBr(uint32_t i) { pc = regs[getrd(i)]; }
void execBrrReg(uint32_t i) { pc += regs[getrd(i)]; }
void execBrrImm(uint32_t i) { pc += getL(i); }
void execBrnz(uint32_t i) {
    if (regs[getrs(i)] == 0)
        NEXT;
    else
        pc = regs[getrd(i)];
}
void execCall(uint32_t i) {
    uint64_t retAddr = pc + INC;
    store64(regs[31] - 8, retAddr); // dont modify
    pc = regs[getrd(i)];
}
void execReturn() {
    uint64_t retAddr = load64(regs[31] - 8); // dont modify r31
    pc = retAddr;
}

// main loop
void runSim(void) {

    while (running) {

        uint32_t instr = fetchInstr();
        uint32_t op = getOpcode(instr);

        switch (op) {
        // Logic operations
        case 0x00:
            execAnd(instr);
            break;

        case 0x01:
            execOr(instr);
            break;

        case 0x02:
            execXor(instr);
            break;

        case 0x03:
            execNot(instr);
            break;

        // Shifts
        case 0x04:
            execShftr(instr);
            break;

        case 0x05:
            execShftri(instr);
            break;

        case 0x06:
            execShftl(instr);
            break;

        case 0x07:
            execShftli(instr);
            break;

        // Control flow
        case 0x08:
            execBr(instr);
            break;

        case 0x09:
            execBrrReg(instr);
            break;

        case 0x0A:
            execBrrImm(instr);
            break;

        case 0x0B:
            execBrnz(instr);
            break;

        case 0x0C:
            execCall(instr);
            break;

        case 0x0D:
            execReturn();
            break;

        case 0x0E:
            execBrgt(instr);
            break;

        case 0x0F:
            execPriv(instr);
            break;

        // Memory operations
        case 0x10:
            execMovLoad(instr);
            break;

        case 0x11:
            execMovReg(instr);
            break;

        case 0x12:
            execMovImm(instr);
            break;

        case 0x13:
            execMovStore(instr);
            break;

        // Floating point
        case 0x14:
            execAddf(instr);
            break;

        case 0x15:
            execSubf(instr);
            break;

        case 0x16:
            execMulf(instr);
            break;

        case 0x17:
            execDivf(instr);
            break;

        // Arithmetic
        case 0x18:
            execAdd(instr);
            break;

        case 0x19:
            execAddi(instr);
            break;

        case 0x1A:
            execSub(instr);
            break;

        case 0x1B:
            execSubi(instr);
            break;

        case 0x1C:
            execMul(instr);
            break;

        case 0x1D:
            execDiv(instr);
            break;

        default:
            fprintf(stderr, "Simulation error\n");
            exit(1);
        }
    }
}

// load file: reads tinker file header, loads code and data segments into memory
int procFile(const char *file) {
    FILE *f = fopen(file, "rb");
    if (!f)
        return 1;

    // read the header
    TinkerFileHeader header;
    if (fread(&header, sizeof(TinkerFileHeader), 1, f) != 1) {
        fprintf(stderr, "Simulation error\n");
        fclose(f);
        return 1;
    }

    // check segment overlap
    uint64_t code_end = header.code_seg_begin + header.code_seg_size;
    uint64_t data_end = header.data_seg_begin + header.data_seg_size;

    if (header.data_seg_size > 0 && !(code_end <= header.data_seg_begin ||
                                      data_end <= header.code_seg_begin)) {

        fprintf(stderr, "Simulation error\n");
        fclose(f);
        return 1;
    }

    // validate file type
    if (header.file_type != 0) {
        fprintf(stderr, "Simulation error\n");
        fclose(f);
        return 1;
    }

    // validate that segments fit within memory
    if (header.code_seg_begin + header.code_seg_size > MEM_SIZE) {
        fprintf(stderr,
                "Simulation error\n");
        fclose(f);
        return 1;
    }

    if (header.data_seg_size > 0 &&
        header.data_seg_begin + header.data_seg_size > MEM_SIZE) {
        fprintf(stderr,
                "Simulation error\n");
        fclose(f);
        return 1;
    }

    // load code segment into memory at code_seg_begin
    if (header.code_seg_size > 0) {
        if (fread(mem + header.code_seg_begin, 1, header.code_seg_size, f) !=
            header.code_seg_size) {
            fprintf(stderr,
                    "Simulation error\n");
            fclose(f);
            return 1;
        }
    }

    // load data segment into memory at data_seg_begin
    if (header.data_seg_size > 0) {
        if (fread(&mem[header.data_seg_begin], 1, header.data_seg_size, f) !=
            header.data_seg_size) {
            fprintf(stderr,
                    "Simulation error\n");
            fclose(f);
            return 1;
        }
    }

    pc = header.code_seg_begin;

    fclose(f);
    return 0;
}

// main
int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file>\n", argv[0]);
        return 1;
    }

    initMachine();

    if (procFile(argv[1]) != 0) {
        fprintf(stderr, "Invalid tinker filepath\n");
        return 1;
    }

    runSim();
    return 0;
}