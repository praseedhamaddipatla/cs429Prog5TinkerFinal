#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 256
#define MAX_TOKENS 4
#define MAX_LABELS 512
#define MAX_INSTS 2048

#define DEBUG 0


// segment base addresses
#define CODE_START_ADDR 0x2000
#define DATA_START_ADDR 0x10000

// global arrays for labels and instructions
typedef struct {
    char name[50];
    int addr;
} Label;

typedef struct {
    char op[12];
    char args[4][20];
    int numArgs;
    int addr;
    int isCode; // 1 for code, 0 for data
    uint64_t dataValue;
} Instruction;

Label labels[MAX_LABELS];
int numLabels = 0;

Instruction instructions[MAX_INSTS];
int numInstructions = 0;

char *binaryFile;

// tinker file header
typedef struct {
    uint64_t file_type;
    uint64_t code_seg_begin;
    uint64_t code_seg_size;
    uint64_t data_seg_begin;
    uint64_t data_seg_size;
} TinkerFileHeader;

//  utility functions

void cleanupAndExit() {
    // remove the partially-written output if it exists
    if (binaryFile)
        remove(binaryFile);

    exit(1);
}

void cleanLine(char *s) { s[strcspn(s, "\n")] = 0; }

void addLabel(const char *name, int addr) {
    strcpy(labels[numLabels].name, name);
    labels[numLabels].addr = addr;
    numLabels++;
}

int findLabel(const char *name) {
    for (int i = 0; i < numLabels; i++) {
        if (strcmp(labels[i].name, name) == 0)
            return labels[i].addr;
    }
    return -1;
}

void addInstruction(Instruction inst) {
    if (numInstructions >= MAX_INSTS) {
        fprintf(stderr, "too many instructions\n");
        cleanupAndExit();
    }
    instructions[numInstructions++] = inst;
}

int getRegisterNumber(const char *s) {
    if (s[0] != 'r')
        return -1;
    return atoi(s + 1);
}

void verifyArgs(Instruction inst, int expected) {
    if (inst.numArgs != expected) {
        fprintf(stderr, "wrong number of args for %s\n", inst.op);
    }
}

//  first pass – collect label addresses

int calculateInstructionSize(const char *opcode) {
    if (strcmp(opcode, "ld") == 0)
        return 48;
    if (strcmp(opcode, "push") == 0)
        return 8;
    if (strcmp(opcode, "pop") == 0)
        return 8;
    return 4;
}

void firstPass(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "cant open file\n");
        cleanupAndExit();
    }

    char buffer[MAX_LINE];
    int codeAddress = CODE_START_ADDR;
    int dataAddress = DATA_START_ADDR;
    int mode = -1; // -1 = unknown, 0 = data, 1 = code

    while (fgets(buffer, sizeof(buffer), f)) {
        cleanLine(buffer);

        if (buffer[0] == ';' || strlen(buffer) == 0)
            continue;

        if (buffer[0] == ':') {
            int i = 1;

            if (buffer[i] == '\0' || buffer[i] == '\n') {
                fprintf(stderr, "error: empty label\n");
                cleanupAndExit();
            }
            if (buffer[i] == ' ' || buffer[i] == '\t') {
                fprintf(stderr, "error: invalid label\n");
                cleanupAndExit();
            }
            if (!isalpha((unsigned char)buffer[i]) && buffer[i] != '_') {
                fprintf(stderr, "error: invalid label\n");
                cleanupAndExit();
            }

            int start = i;
            while (isalnum((unsigned char)buffer[i]) || buffer[i] == '_')
                i++;

            if (i == start) {
                fprintf(stderr, "error: invalid label\n");
                cleanupAndExit();
            }

            while (buffer[i] == ' ' || buffer[i] == '\t')
                i++;

            if (buffer[i] != '\0' && buffer[i] != '\n') {
                fprintf(stderr, "error: invalid label\n");
                cleanupAndExit();
            }

            int labelAddr = (mode == 1) ? codeAddress : dataAddress;
            addLabel(buffer, labelAddr);
            continue;
        }

        if (buffer[0] == '.') {
            if (buffer[1] == 'c')
                mode = 1;
            else if (buffer[1] == 'd')
                mode = 0;
            continue;
        }

        if (buffer[0] == '\t') {
            char *line = buffer + 1;

            if (mode == 1) {
                char temp[MAX_LINE];
                strcpy(temp, line);
                char *opcode = strtok(temp, " ,");
                if (opcode)
                    codeAddress += calculateInstructionSize(opcode);
            } else if (mode == 0) {
                dataAddress += 8;
            }
        }
    }

    fclose(f);
}

//  macro expansion helpers

void expandLoadInstruction(Instruction current) {
    uint64_t value = strtoull(current.args[1], NULL, 0);
    Instruction temp;
    memset(&temp, 0, sizeof(Instruction));
    temp.isCode = 1;

    // xor rd, rd, rd  (clear register)
    strcpy(temp.op, "xor");
    strcpy(temp.args[0], current.args[0]);
    strcpy(temp.args[1], current.args[0]);
    strcpy(temp.args[2], current.args[0]);
    temp.numArgs = 3;
    temp.addr = current.addr;
    addInstruction(temp);
    int address = current.addr + 4;

    // load 64-bit literal in 12-bit chunks, high to low
    int shifts[] = {52, 40, 28, 16};
    for (int i = 0; i < 4; i++) {
        strcpy(temp.op, "addi");
        snprintf(temp.args[1], sizeof(temp.args[1]), "%llu",
                 (unsigned long long)((value >> shifts[i]) & 0xFFF));
        temp.args[2][0] = '\0';
        temp.numArgs = 2;
        temp.addr = address;
        addInstruction(temp);
        address += 4;

        strcpy(temp.op, "shftli");
        strcpy(temp.args[1], "12");
        temp.addr = address;
        addInstruction(temp);
        address += 4;
    }

    // last chunk: bits [7:4]
    strcpy(temp.op, "addi");
    snprintf(temp.args[1], sizeof(temp.args[1]), "%llu",
             (unsigned long long)((value >> 4) & 0xFFF));
    temp.args[2][0] = '\0';
    temp.numArgs = 2;
    temp.addr = address;
    addInstruction(temp);
    address += 4;

    strcpy(temp.op, "shftli");
    strcpy(temp.args[1], "4");
    temp.addr = address;
    addInstruction(temp);
    address += 4;

    // final chunk: bits [3:0]
    strcpy(temp.op, "addi");
    snprintf(temp.args[1], sizeof(temp.args[1]), "%llu",
             (unsigned long long)(value & 0xF));
    temp.args[2][0] = '\0';
    temp.addr = address;
    addInstruction(temp);
}

int tryExpandMacro(Instruction current) {
    Instruction temp;
    memset(&temp, 0, sizeof(Instruction));
    temp.isCode = 1;

    if (strcmp(current.op, "in") == 0) {
        verifyArgs(current, 2);
        strcpy(current.op, "priv");
        strcpy(current.args[2], "r0");
        strcpy(current.args[3], "3");
        current.numArgs = 4;
        addInstruction(current);
        return current.addr + 4;
    }

    if (strcmp(current.op, "clr") == 0) {
        verifyArgs(current, 1);
        strcpy(current.op, "xor");
        strcpy(current.args[1], current.args[0]);
        strcpy(current.args[2], current.args[0]);
        current.numArgs = 3;
        addInstruction(current);
        return current.addr + 4;
    }

    if (strcmp(current.op, "out") == 0) {
        verifyArgs(current, 2);
        strcpy(current.op, "priv");
        strcpy(current.args[2], "r0");
        strcpy(current.args[3], "4");
        current.numArgs = 4;
        addInstruction(current);
        return current.addr + 4;
    }

    if (strcmp(current.op, "ld") == 0) {
        verifyArgs(current, 2);
        expandLoadInstruction(current);
        return current.addr + 48;
    }

    if (strcmp(current.op, "push") == 0) {
        verifyArgs(current, 1);
        strcpy(temp.op, "mov");
        strcpy(temp.args[0], "(r31)(-8)");
        strcpy(temp.args[1], current.args[0]);
        temp.numArgs = 2;
        temp.addr = current.addr;
        addInstruction(temp);

        strcpy(temp.op, "subi");
        strcpy(temp.args[0], "r31");
        strcpy(temp.args[1], "8");
        temp.numArgs = 2;
        temp.addr = current.addr + 4;
        addInstruction(temp);
        return current.addr + 8;
    }

    if (strcmp(current.op, "pop") == 0) {
        verifyArgs(current, 1);
        strcpy(temp.op, "mov");
        strcpy(temp.args[0], current.args[0]);
        strcpy(temp.args[1], "(r31)(0)");
        temp.numArgs = 2;
        temp.addr = current.addr;
        addInstruction(temp);

        strcpy(temp.op, "addi");
        strcpy(temp.args[0], "r31");
        strcpy(temp.args[1], "8");
        temp.numArgs = 2;
        temp.addr = current.addr + 4;
        addInstruction(temp);
        return current.addr + 8;
    }

    if (strcmp(current.op, "halt") == 0) {
        strcpy(temp.op, "priv");
        strcpy(temp.args[0], "r0");
        strcpy(temp.args[1], "r0");
        strcpy(temp.args[2], "r0");
        strcpy(temp.args[3], "0");
        temp.numArgs = 4;
        temp.addr = current.addr;
        addInstruction(temp);
        return current.addr + 4;
    }

    // not a macro – pass through
    addInstruction(current);
    return current.addr + 4;
}

//  second pass – parse instructions and expand macros

int parseCodeLine(char *line, int address) {
    const char *delimiter = " ,";
    char *token = strtok(line, delimiter);

    if (!token) {
        fprintf(stderr, "empty instruction\n");
        cleanupAndExit();
    }

    Instruction current;
    memset(&current, 0, sizeof(Instruction));
    strcpy(current.op, token);
    current.addr = address;
    current.isCode = 1;

    int numArgs = 0;
    token = strtok(NULL, delimiter);
    while (token && numArgs < 4) {
        if (token[0] == 'r' && isdigit((unsigned char)token[1])) {
            int registerNum = atoi(token + 1);
            if (registerNum < 0 || registerNum > 31) {
                fprintf(stderr, "invalid register number\n");
                cleanupAndExit();
            }
        }

        if (token[0] == ':') {
            int labelAddress = findLabel(token);
            if (labelAddress == -1) {
                fprintf(stderr, "reference to nonexistent label: %s\n", token);
                cleanupAndExit();
            }
            snprintf(current.args[numArgs], sizeof(current.args[numArgs]), "%d",
                     labelAddress);
        } else {
            strcpy(current.args[numArgs], token);
        }

        numArgs++;
        token = strtok(NULL, delimiter);
    }

    current.numArgs = numArgs;
    return tryExpandMacro(current);
}

int parseDataLine(char *line, int address) {
    if (line[0] == '-') {
        fprintf(stderr, "data must be unsigned\n");
        cleanupAndExit();
    }

    Instruction current;
    memset(&current, 0, sizeof(Instruction));
    current.addr = address;
    current.isCode = 0;
    current.dataValue = strtoull(line, NULL, 0);

    addInstruction(current);
    return address + 8;
}

void secondPass(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "cant open file\n");
        cleanupAndExit();
    }

    char buffer[MAX_LINE];
    int codeAddress = CODE_START_ADDR;
    int dataAddress = DATA_START_ADDR;
    int mode = -1;

    while (fgets(buffer, sizeof(buffer), f)) {
        cleanLine(buffer);

        if (buffer[0] == ';' || strlen(buffer) == 0)
            continue;

        if (buffer[0] == ':')
            continue; // labels already handled in first pass

        if (buffer[0] == '.') {
            Instruction marker;
            memset(&marker, 0, sizeof(Instruction));

            if (strcmp(buffer, ".code") == 0) {
                mode = 1;
                marker.isCode = 1;
                strcpy(marker.op, ".code");
            } else if (strcmp(buffer, ".data") == 0) {
                mode = 0;
                marker.isCode = 0;
                strcpy(marker.op, ".data");
            }
            addInstruction(marker);
            continue;
        }

        if (buffer[0] == '\t') {
            char *line = buffer + 1;

            if (mode == 1)
                codeAddress = parseCodeLine(line, codeAddress);
            else if (mode == 0)
                dataAddress = parseDataLine(line, dataAddress);
        }
    }

    fclose(f);
}

//  binary output

void writeBinaryInstruction(FILE *f, int opcode, char *rd, char *rs, char *rt,
                            char *immediate) {
    unsigned int binaryInstruction = 0;

    binaryInstruction |= (opcode & 0x1F) << 27;

    if (rd) {
        int registerNum = atoi(rd + 1);
        binaryInstruction |= (registerNum & 0x1F) << 22;
    }
    if (rs) {
        int registerNum = atoi(rs + 1);
        binaryInstruction |= (registerNum & 0x1F) << 17;
    }
    if (rt) {
        int registerNum = atoi(rt + 1);
        binaryInstruction |= (registerNum & 0x1F) << 12;
    }
    if (immediate) {
        int value = atoi(immediate);
        binaryInstruction |= (value & 0xFFF);
    }

    fwrite(&binaryInstruction, 4, 1, f);
}

void encodeInstruction(Instruction inst, FILE *f) {
    // arithmetic
    if (strcmp(inst.op, "add") == 0) {
        verifyArgs(inst, 3);
        writeBinaryInstruction(f, 0x18, inst.args[0], inst.args[1],
                               inst.args[2], NULL);
    } else if (strcmp(inst.op, "addi") == 0) {
        verifyArgs(inst, 2);
        writeBinaryInstruction(f, 0x19, inst.args[0], NULL, NULL, inst.args[1]);
    } else if (strcmp(inst.op, "sub") == 0) {
        verifyArgs(inst, 3);
        writeBinaryInstruction(f, 0x1a, inst.args[0], inst.args[1],
                               inst.args[2], NULL);
    } else if (strcmp(inst.op, "subi") == 0) {
        verifyArgs(inst, 2);
        writeBinaryInstruction(f, 0x1b, inst.args[0], NULL, NULL, inst.args[1]);
    } else if (strcmp(inst.op, "mul") == 0) {
        verifyArgs(inst, 3);
        writeBinaryInstruction(f, 0x1c, inst.args[0], inst.args[1],
                               inst.args[2], NULL);
    } else if (strcmp(inst.op, "div") == 0) {
        verifyArgs(inst, 3);
        writeBinaryInstruction(f, 0x1d, inst.args[0], inst.args[1],
                               inst.args[2], NULL);
    }

    // logic
    else if (strcmp(inst.op, "and") == 0) {
        verifyArgs(inst, 3);
        writeBinaryInstruction(f, 0x0, inst.args[0], inst.args[1], inst.args[2],
                               NULL);
    } else if (strcmp(inst.op, "or") == 0) {
        verifyArgs(inst, 3);
        writeBinaryInstruction(f, 0x1, inst.args[0], inst.args[1], inst.args[2],
                               NULL);
    } else if (strcmp(inst.op, "xor") == 0) {
        verifyArgs(inst, 3);
        writeBinaryInstruction(f, 0x2, inst.args[0], inst.args[1], inst.args[2],
                               NULL);
    } else if (strcmp(inst.op, "not") == 0) {
        verifyArgs(inst, 2);
        writeBinaryInstruction(f, 0x3, inst.args[0], inst.args[1], NULL, NULL);
    } else if (strcmp(inst.op, "shftr") == 0) {
        verifyArgs(inst, 3);
        writeBinaryInstruction(f, 0x4, inst.args[0], inst.args[1], inst.args[2],
                               NULL);
    } else if (strcmp(inst.op, "shftri") == 0) {
        verifyArgs(inst, 2);
        writeBinaryInstruction(f, 0x5, inst.args[0], NULL, NULL, inst.args[1]);
    } else if (strcmp(inst.op, "shftl") == 0) {
        verifyArgs(inst, 3);
        writeBinaryInstruction(f, 0x6, inst.args[0], inst.args[1], inst.args[2],
                               NULL);
    } else if (strcmp(inst.op, "shftli") == 0) {
        verifyArgs(inst, 2);
        writeBinaryInstruction(f, 0x7, inst.args[0], NULL, NULL, inst.args[1]);
    }

    // control flow
    else if (strcmp(inst.op, "br") == 0) {
        verifyArgs(inst, 1);
        writeBinaryInstruction(f, 0x8, inst.args[0], NULL, NULL, NULL);
    } else if (strcmp(inst.op, "brr") == 0) {
        verifyArgs(inst, 1);
        if (inst.args[0][0] == 'r')
            writeBinaryInstruction(f, 0x9, inst.args[0], NULL, NULL, NULL);
        else
            writeBinaryInstruction(f, 0xa, NULL, NULL, NULL, inst.args[0]);
    } else if (strcmp(inst.op, "brnz") == 0) {
        verifyArgs(inst, 2);
        writeBinaryInstruction(f, 0xb, inst.args[0], inst.args[1], NULL, NULL);
    } else if (strcmp(inst.op, "call") == 0) {
        verifyArgs(inst, 3);
        writeBinaryInstruction(f, 0xc, inst.args[0], inst.args[1], inst.args[2],
                               NULL);
    } else if (strcmp(inst.op, "return") == 0) {
        verifyArgs(inst, 0);
        writeBinaryInstruction(f, 0xd, NULL, NULL, NULL, NULL);
    } else if (strcmp(inst.op, "brgt") == 0) {
        verifyArgs(inst, 3);
        writeBinaryInstruction(f, 0xe, inst.args[0], inst.args[1], inst.args[2],
                               NULL);
    }

    // privileged
    else if (strcmp(inst.op, "priv") == 0) {
        verifyArgs(inst, 4);
        writeBinaryInstruction(f, 0xf, inst.args[0], inst.args[1], inst.args[2],
                               inst.args[3]);
    }

    // floating point
    else if (strcmp(inst.op, "addf") == 0) {
        verifyArgs(inst, 3);
        writeBinaryInstruction(f, 0x14, inst.args[0], inst.args[1],
                               inst.args[2], NULL);
    } else if (strcmp(inst.op, "subf") == 0) {
        verifyArgs(inst, 3);
        writeBinaryInstruction(f, 0x15, inst.args[0], inst.args[1],
                               inst.args[2], NULL);
    } else if (strcmp(inst.op, "mulf") == 0) {
        verifyArgs(inst, 3);
        writeBinaryInstruction(f, 0x16, inst.args[0], inst.args[1],
                               inst.args[2], NULL);
    } else if (strcmp(inst.op, "divf") == 0) {
        verifyArgs(inst, 3);
        writeBinaryInstruction(f, 0x17, inst.args[0], inst.args[1],
                               inst.args[2], NULL);
    }

    // mov
    else if (strcmp(inst.op, "mov") == 0) {
        verifyArgs(inst, 2);

        if (inst.args[0][0] == '(') {
            // mov (rd)(L), rs  →  store
            char register1[10], offset[10];
            sscanf(inst.args[0], "(%[^)])(%[^)])", register1, offset);
            writeBinaryInstruction(f, 0x13, register1, inst.args[1], NULL,
                                   offset);
        } else if (inst.args[1][0] == '(') {
            // mov rd, (rs)(L)  →  load
            char register2[10], offset[10];
            sscanf(inst.args[1], "(%[^)])(%[^)])", register2, offset);
            writeBinaryInstruction(f, 0x10, inst.args[0], register2, NULL,
                                   offset);
        } else {
            // mov rd, rs  or  mov rd, L
            if (inst.args[1][0] == 'r')
                writeBinaryInstruction(f, 0x11, inst.args[0], inst.args[1],
                                       NULL, NULL);
            else
                writeBinaryInstruction(f, 0x12, inst.args[0], NULL, NULL,
                                       inst.args[1]);
        }
    }
}

void writeBinary(const char *filename)
{
    FILE *f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "cant write binary\n");
        cleanupAndExit();
    }

    uint64_t codeSize = 0;
    uint64_t dataSize = 0;

    for (int i = 0; i < numInstructions; i++)
    {
        if (instructions[i].op[0] == '.')
            continue;

        if (instructions[i].isCode)
            codeSize += 4;
        else
            dataSize += 8;
    }

    TinkerFileHeader header;
    header.file_type = 0;
    header.code_seg_begin = CODE_START_ADDR;   // 0x2000
    header.code_seg_size  = codeSize;
    header.data_seg_begin = DATA_START_ADDR;   // 0x10000
    header.data_seg_size  = dataSize;

    // Write header
    fwrite(&header, sizeof(header), 1, f);

    // Write code segment IMMEDIATELY after header (no padding!)
    for (int i = 0; i < numInstructions; i++)
    {
        if (instructions[i].op[0] == '.')
            continue;
        if (instructions[i].isCode)
            encodeInstruction(instructions[i], f);
    }

    // Write data segment IMMEDIATELY after code (no padding!)
    for (int i = 0; i < numInstructions; i++)
    {
        if (instructions[i].op[0] == '.')
            continue;
        if (!instructions[i].isCode)
            fwrite(&instructions[i].dataValue, 8, 1, f);
    }

    fclose(f);
}

//  validation

int hasError = 0;

int isNegative(const char *str) { return str[0] == '-'; }

int validateRegister(const char *reg) {
    if (reg[0] != 'r') {
        fprintf(stderr, "error: invalid register '%s'\n", reg);
        hasError = 1;
        return -1;
    }
    if (!isdigit((unsigned char)reg[1])) {
        fprintf(stderr, "error: invalid register '%s'\n", reg);
        hasError = 1;
        return -1;
    }

    int regNum = atoi(reg + 1);
    if (regNum < 0 || regNum > 31) {
        fprintf(stderr, "error: register number must be 0-31, got %d\n",
                regNum);
        hasError = 1;
        return -1;
    }
    return regNum;
}

uint64_t parseNumber(const char *str) {
    if (str[0] == ':')
        return 0; // label – resolved later

    char *end;
    uint64_t value = strtoull(str, &end, 0);

    if (end == str || *end != '\0') {
        fprintf(stderr, "error: invalid number '%s'\n", str);
        hasError = 1;
        return 0;
    }
    return value;
}

void validateMacroArgs(const char *macroName, int expected, int actual) {
    if (actual != expected) {
        fprintf(stderr, "error: macro '%s' expects %d args, got %d\n",
                macroName, expected, actual);
        hasError = 1;
    }
}

void validateFile(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "error: cannot open file for validation\n");
        hasError = 1;
        return;
    }

    char line[MAX_LINE];
    int currentMode = -1;
    int lineNum = 0;
    int hasCode = 0;

    while (fgets(line, sizeof(line), f)) {
        lineNum++;

        char rawLine[MAX_LINE];
        strcpy(rawLine, line);

        cleanLine(line);

        // empty lines and comments
        if (line[0] == '\0' || line[0] == ';')
            continue;

        // leading spaces are not allowed
        if (line[0] == ' ') {
            fprintf(stderr, "error line %d: instructions must start with tab\n",
                    lineNum);
            hasError = 1;
            fclose(f);
            return;
        }

        // section directives
        if (line[0] == '.') {
            if (strcmp(line, ".code") == 0) {
                currentMode = 1;
                hasCode = 1;
            } else if (strcmp(line, ".data") == 0) {
                currentMode = 0;
            } else {
                fprintf(stderr, "error line %d: invalid directive '%s'\n",
                        lineNum, line);
                hasError = 1;
                fclose(f);
                return;
            }
            continue;
        }

        // labels
        if (rawLine[0] == ':') {
            int i = 1;

            if (rawLine[i] == '\n' || rawLine[i] == '\0') {
                fprintf(stderr, "error line %d: empty label\n", lineNum);
                hasError = 1;
                fclose(f);
                return;
            }
            if (rawLine[i] == ' ' || rawLine[i] == '\t') {
                fprintf(stderr, "error line %d: invalid label\n", lineNum);
                hasError = 1;
                fclose(f);
                return;
            }

            int start = i;
            while (!isspace(rawLine[i]))
                i++;

            if (i == start) {
                fprintf(stderr, "error line %d: invalid label\n", lineNum);
                hasError = 1;
                fclose(f);
                return;
            }

            while (rawLine[i] == ' ' || rawLine[i] == '\t')
                i++;

            if (rawLine[i] != '\n' && rawLine[i] != '\0') {
                fprintf(stderr, "error line %d: invalid label\n", lineNum);
                hasError = 1;
                fclose(f);
                return;
            }

            continue;
        }

        // instructions / data
        if (line[0] == '\t') {
            char *content = line + 1;

            // inline labels are not allowed
            if (content[0] == ':') {
                fprintf(stderr,
                        "error line %d: inline labels are not allowed\n",
                        lineNum);
                hasError = 1;
                fclose(f);
                return;
            }

            if (currentMode == 1) {
                //  code section

                // check parentheses balance and ordering
                {
                    int openCount = 0;
                    int closeCount = 0;
                    int lastWasOpen = 0;
                    int lastWasClose = 0;

                    for (char *p = content; *p; p++) {
                        if (*p == '(') {
                            openCount++;
                            if (lastWasOpen) {
                                fprintf(stderr,
                                        "error line %d: consecutive '((' in "
                                        "instruction\n",
                                        lineNum);
                                hasError = 1;
                                fclose(f);
                                return;
                            }
                            lastWasOpen = 1;
                            lastWasClose = 0;
                        } else if (*p == ')') {
                            closeCount++;
                            if (closeCount > openCount) {
                                fprintf(
                                    stderr,
                                    "error line %d: ')' before matching '('\n",
                                    lineNum);
                                hasError = 1;
                                fclose(f);
                                return;
                            }
                            if (lastWasClose) {
                                fprintf(stderr,
                                        "error line %d: consecutive '))' in "
                                        "instruction\n",
                                        lineNum);
                                hasError = 1;
                                fclose(f);
                                return;
                            }
                            lastWasOpen = 0;
                            lastWasClose = 1;
                        } else if (*p != ' ' && *p != '\t' && *p != ',') {
                            lastWasOpen = 0;
                            lastWasClose = 0;
                        }
                    }

                    if (openCount != closeCount) {
                        fprintf(stderr,
                                "error line %d: mismatched parentheses "
                                "(open=%d, close=%d)\n",
                                lineNum, openCount, closeCount);
                        hasError = 1;
                        fclose(f);
                        return;
                    }
                }

                // tokenize
                char buffer[MAX_LINE];
                strcpy(buffer, content);

                const char *delim = " ,()";
                char *token = strtok(buffer, delim);
                if (!token)
                    continue;

                char tokens[10][MAX_LINE];
                int numTokens = 0;

                strcpy(tokens[numTokens++], token);
                while ((token = strtok(NULL, delim)) != NULL && numTokens < 10)
                    strcpy(tokens[numTokens++], token);

                if (token != NULL) {
                    fprintf(stderr, "error line %d: too many tokens\n",
                            lineNum);
                    hasError = 1;
                    fclose(f);
                    return;
                }

                char *opcode = tokens[0];
                int argCount = numTokens - 1;

                //  per-opcode validation

                if (strcmp(opcode, "halt") == 0) {
                    validateMacroArgs("halt", 0, argCount);
                } else if (strcmp(opcode, "in") == 0) {
                    validateMacroArgs("in", 2, argCount);
                    if (!hasError && argCount >= 2) {
                        validateRegister(tokens[1]);
                        validateRegister(tokens[2]);
                    }
                } else if (strcmp(opcode, "out") == 0) {
                    validateMacroArgs("out", 2, argCount);
                    if (!hasError && argCount >= 2) {
                        validateRegister(tokens[1]);
                        validateRegister(tokens[2]);
                    }
                } else if (strcmp(opcode, "clr") == 0) {
                    validateMacroArgs("clr", 1, argCount);
                    if (!hasError && argCount >= 1)
                        validateRegister(tokens[1]);
                } else if (strcmp(opcode, "push") == 0) {
                    validateMacroArgs("push", 1, argCount);
                    if (!hasError && argCount >= 1)
                        validateRegister(tokens[1]);
                } else if (strcmp(opcode, "pop") == 0) {
                    validateMacroArgs("pop", 1, argCount);
                    if (!hasError && argCount >= 1)
                        validateRegister(tokens[1]);
                } else if (strcmp(opcode, "ld") == 0) {
                    validateMacroArgs("ld", 2, argCount);
                    if (!hasError && argCount >= 2) {
                        validateRegister(tokens[1]);
                        if (tokens[2][0] != ':') {
                            if (isNegative(tokens[2])) {
                                fprintf(
                                    stderr,
                                    "error: 'ld' literal cannot be negative\n");
                                hasError = 1;
                            } else {
                                parseNumber(tokens[2]);
                            }
                        }
                    }
                }
                // three-register instructions
                else if (strcmp(opcode, "add") == 0 ||
                         strcmp(opcode, "sub") == 0 ||
                         strcmp(opcode, "mul") == 0 ||
                         strcmp(opcode, "div") == 0 ||
                         strcmp(opcode, "and") == 0 ||
                         strcmp(opcode, "or") == 0 ||
                         strcmp(opcode, "xor") == 0 ||
                         strcmp(opcode, "shftr") == 0 ||
                         strcmp(opcode, "shftl") == 0 ||
                         strcmp(opcode, "addf") == 0 ||
                         strcmp(opcode, "subf") == 0 ||
                         strcmp(opcode, "mulf") == 0 ||
                         strcmp(opcode, "divf") == 0) {
                    if (argCount != 3) {
                        fprintf(stderr,
                                "error: instruction '%s' expects 3 args\n",
                                opcode);
                        hasError = 1;
                    } else {
                        validateRegister(tokens[1]);
                        validateRegister(tokens[2]);
                        validateRegister(tokens[3]);
                    }
                }
                // register + 12-bit unsigned immediate
                else if (strcmp(opcode, "addi") == 0 ||
                         strcmp(opcode, "subi") == 0 ||
                         strcmp(opcode, "shftli") == 0 ||
                         strcmp(opcode, "shftri") == 0) {
                    if (argCount != 2) {
                        fprintf(stderr,
                                "error: instruction '%s' expects 2 args\n",
                                opcode);
                        hasError = 1;
                    } else {
                        validateRegister(tokens[1]);
                        if (tokens[2][0] != ':') {
                            errno = 0;
                            char *end;
                            unsigned long long val =
                                strtoull(tokens[2], &end, 0);
                            if (end == tokens[2] || *end != '\0') {
                                fprintf(stderr,
                                        "error: invalid immediate value '%s'\n",
                                        tokens[2]);
                                hasError = 1;
                            } else if (errno == ERANGE || val > 4095) {
                                fprintf(stderr,
                                        "error: immediate out of range (must "
                                        "be 0-4095): %s\n",
                                        tokens[2]);
                                hasError = 1;
                            }
                        }
                    }
                } else if (strcmp(opcode, "not") == 0) {
                    if (argCount != 2) {
                        fprintf(stderr,
                                "error: instruction 'not' expects 2 args\n");
                        hasError = 1;
                    } else {
                        validateRegister(tokens[1]);
                        validateRegister(tokens[2]);
                    }
                } else if (strcmp(opcode, "br") == 0) {
                    if (argCount != 1) {
                        fprintf(stderr,
                                "error: instruction 'br' expects 1 arg\n");
                        hasError = 1;
                    } else {
                        validateRegister(tokens[1]);
                    }
                } else if (strcmp(opcode, "brr") == 0) {
                    if (argCount != 1) {
                        fprintf(stderr,
                                "error: instruction 'brr' expects 1 arg\n");
                        hasError = 1;
                    } else {
                        if (tokens[1][0] == 'r') {
                            // register form: brr rd
                            validateRegister(tokens[1]);
                        } else if (tokens[1][0] == ':') {
                            // label form: validated later in second pass
                        } else {
                            // literal form: must be 12-bit signed  –2048 … 2047
                            errno = 0;
                            char *end;
                            long long val = strtoll(tokens[1], &end, 0);

                            if (end == tokens[1] || *end != '\0') {
                                fprintf(stderr,
                                        "error: invalid immediate value '%s'\n",
                                        tokens[1]);
                                hasError = 1;
                            } else if (errno == ERANGE || val < -2048 ||
                                       val > 2047) {
                                fprintf(stderr,
                                        "error: brr immediate out of range "
                                        "(must be -2048 to 2047): %s\n",
                                        tokens[1]);
                                hasError = 1;
                            }
                        }
                    }
                } else if (strcmp(opcode, "brnz") == 0) {
                    if (argCount != 2) {
                        fprintf(stderr,
                                "error: instruction 'brnz' expects 2 args\n");
                        hasError = 1;
                    } else {
                        validateRegister(tokens[1]);
                        validateRegister(tokens[2]);
                    }
                } else if (strcmp(opcode, "call") == 0) {
                    if (argCount != 3) {
                        fprintf(stderr,
                                "error: instruction 'call' expects 3 args\n");
                        hasError = 1;
                    } else {
                        validateRegister(tokens[1]);
                        validateRegister(tokens[2]);
                        validateRegister(tokens[3]);
                    }
                } else if (strcmp(opcode, "return") == 0) {
                    if (argCount != 0) {
                        fprintf(stderr,
                                "error: instruction 'return' expects 0 args\n");
                        hasError = 1;
                    }
                } else if (strcmp(opcode, "brgt") == 0) {
                    if (argCount != 3) {
                        fprintf(stderr,
                                "error: instruction 'brgt' expects 3 args\n");
                        hasError = 1;
                    } else {
                        validateRegister(tokens[1]);
                        validateRegister(tokens[2]);
                        validateRegister(tokens[3]);
                    }
                } else if (strcmp(opcode, "priv") == 0) {
                    if (argCount != 4) {
                        fprintf(stderr,
                                "error: instruction 'priv' expects 4 args\n");
                        hasError = 1;
                    } else {
                        validateRegister(tokens[1]);
                        validateRegister(tokens[2]);
                        validateRegister(tokens[3]);
                        if (tokens[4][0] != ':')
                            parseNumber(tokens[4]);
                    }
                } else if (strcmp(opcode, "mov") == 0) {
                    if (argCount < 2) {
                        fprintf(stderr, "error: instruction 'mov' expects at "
                                        "least 2 args\n");
                        hasError = 1;
                    }
                    // parentheses consistency already checked above;
                    // nothing more needed at this stage
                } else {
                    fprintf(stderr, "error: unknown instruction '%s'\n",
                            opcode);
                    hasError = 1;
                }

                if (hasError) {
                    fclose(f);
                    return;
                }
            } else if (currentMode == 0) {
                //  data section
                if (isNegative(content)) {
                    fprintf(stderr,
                            "error line %d: data values must be unsigned\n",
                            lineNum);
                    hasError = 1;
                    fclose(f);
                    return;
                }

                errno = 0;
                char *end;
                /*unsigned long long val =*/strtoull(content, &end, 0);

                while (*end == ' ' || *end == '\t')
                    end++;

                if (*end != '\0') {
                    fprintf(stderr, "error line %d: invalid data value '%s'\n",
                            lineNum, content);
                    hasError = 1;
                    fclose(f);
                    return;
                }
                if (errno == ERANGE) {
                    fprintf(stderr,
                            "error line %d: data value out of 64-bit range\n",
                            lineNum);
                    hasError = 1;
                    fclose(f);
                    return;
                }
            } else {
                fprintf(stderr,
                        "error line %d: instruction without .code or .data "
                        "section\n",
                        lineNum);
                hasError = 1;
                fclose(f);
                return;
            }
        } else {
            fprintf(stderr, "error line %d: invalid line format\n", lineNum);
            hasError = 1;
            fclose(f);
            return;
        }
    }

    if (!hasCode) {
        fprintf(stderr, "error: no .code in file\n");
        hasError = 1;
    }

    fclose(f);
}

//  entry point

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <input.tk> <output.tko>\n", argv[0]);
        return 1;
    }

    #if DEBUG
    printf("Header size: %lu\n", sizeof(TinkerFileHeader));
#endif

    binaryFile = argv[2];

    validateFile(argv[1]);
    if (hasError)
        return 1;

    firstPass(argv[1]);
    secondPass(argv[1]);
    writeBinary(argv[2]);

    return 0;
}