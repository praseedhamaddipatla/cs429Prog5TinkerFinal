#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASM "./hw5-asm"
#define SIM "./hw5-sim"

void run_command(const char *cmd) {
    int result = system(cmd);
    assert(result == 0);
}

void read_output(const char *filename, char *buffer, size_t size) {
    FILE *f = fopen(filename, "r");
    assert(f != NULL);
    size_t read = fread(buffer, 1, size - 1, f);
    (void)read;
    fclose(f);
}

//fibonacci tests

void test_fibonacci_case(uint64_t input, uint64_t expected) {
    char cmd[256];
    char output[256] = {0};

    FILE *in = fopen("tmp_input.txt", "w");
    assert(in);
    fprintf(in, "%lu\n", input);
    fclose(in);

    sprintf(cmd, "%s fibonacci.tk fibonacci.tko", ASM);
    run_command(cmd);

    sprintf(cmd, "%s fibonacci.tko < tmp_input.txt > tmp_output.txt", SIM);
    run_command(cmd);

    read_output("tmp_output.txt", output, sizeof(output));

    uint64_t result = strtoull(output, NULL, 10);
    assert(result == expected);
}

void test_fibonacci() {
    printf("Testing Fibonacci...\n");

    test_fibonacci_case(1, 0);
    test_fibonacci_case(2, 1);
    test_fibonacci_case(3, 1);
    test_fibonacci_case(5, 3);
    test_fibonacci_case(10, 34);
    test_fibonacci_case(20, 4181);

    printf("Fibonacci tests passed.\n");
}

//matrix mult tests

void write_matrix_input_2x2() {
    FILE *f = fopen("tmp_input.txt", "w");
    assert(f);

    // dimension
    fprintf(f, "2\n");

    // A = [1 2; 3 4]
    fprintf(f, "4607182418800017408\n"); // 1.0
    fprintf(f, "4611686018427387904\n"); // 2.0
    fprintf(f, "4613937818241073152\n"); // 3.0
    fprintf(f, "4616189618054758400\n"); // 4.0

    // B = [5 6; 7 8]
    fprintf(f, "4617315517961601024\n"); // 5.0
    fprintf(f, "4618441417868443648\n"); // 6.0
    fprintf(f, "4619567317775286272\n"); // 7.0
    fprintf(f, "4620693217682128896\n"); // 8.0

    fclose(f);
}

void test_matrix_multiplication() {
    printf("Testing Matrix Multiplication...\n");

    write_matrix_input_2x2();

    run_command("./hw5-asm matrix_multiplication.tk matrix_multiplication.tko");
    run_command(
        "./hw5-sim matrix_multiplication.tko < tmp_input.txt > tmp_output.txt");

    FILE *f = fopen("tmp_output.txt", "r");
    assert(f);

    uint64_t results[4];
    for (int i = 0; i < 4; i++) {
        assert(fscanf(f, "%lu", &results[i]) == 1);
    }
    fclose(f);

    // Expected:
    // [19 22; 43 50]
    assert(results[0] == 4626041242239631360ULL); // 19.0
    assert(results[1] == 4626885667169763328ULL); // 22.0
    assert(results[2] == 4631248529308778496ULL); // 43.0
    assert(results[3] == 4632233691727265792ULL); // 50.0

    printf("Matrix multiplication tests passed.\n");
}

// binary search tests

void write_binary_input_found() {
    FILE *f = fopen("tmp_input.txt", "w");
    assert(f);

    fprintf(f, "5\n"); // size
    fprintf(f, "1\n");
    fprintf(f, "3\n");
    fprintf(f, "5\n");
    fprintf(f, "7\n");
    fprintf(f, "9\n");
    fprintf(f, "7\n"); // target

    fclose(f);
}

void write_binary_input_not_found() {
    FILE *f = fopen("tmp_input.txt", "w");
    assert(f);

    fprintf(f, "5\n");
    fprintf(f, "1\n");
    fprintf(f, "3\n");
    fprintf(f, "5\n");
    fprintf(f, "7\n");
    fprintf(f, "9\n");
    fprintf(f, "4\n"); // target not present

    fclose(f);
}

void test_binary_search() {
    printf("Testing Binary Search...\n");

    run_command("./hw5-asm binary_search.tk binary_search.tko");

    // Case 1: Found
    write_binary_input_found();
    run_command("./hw5-sim binary_search.tko < tmp_input.txt > tmp_output.txt");

    FILE *f = fopen("tmp_output.txt", "r");
    assert(f);
    char buffer[256];
    char *var1 = fgets(buffer, sizeof(buffer), f);
    (void)var1;
    fclose(f);

    assert(strstr(buffer, "found") != NULL || strstr(buffer, "Found") != NULL);

    // not Found
    write_binary_input_not_found();
    run_command("./hw5-sim binary_search.tko < tmp_input.txt > tmp_output.txt");

    f = fopen("tmp_output.txt", "r");
    assert(f);
    char *var = fgets(buffer, sizeof(buffer), f);
    (void)var;
    fclose(f);

    // message containing "not"
    assert(strstr(buffer, "-1") != NULL || strstr(buffer, "not") != NULL ||
           strstr(buffer, "Not") != NULL);

    printf("Binary search tests passed.\n");
}

//main

int main() {
    printf("\n");
    printf("Running HW5 Integration Tests...\n");
    printf("\n\n");

    test_fibonacci();
    test_binary_search();
    test_matrix_multiplication();

    printf("\nAll HW5 tests passed successfully!\n");

    return 0;
}
