
#include <inttypes.h>
#include <stdio.h>

enum Flags {
    Uppercase = 1,
    Lowercase = 2,
    Digit = 4,
    HexDigit = 8,
    Punctuation = 16,
    Whitespace = 32,
    Control = 64,
};

#include <ctype.h>
int main(void) {
    printf("{");
    for (int c = 0; c < 128; c++) {
        int v = 0;
        if (isupper(c)) {
            v |= Uppercase;
        }
        if (islower(c)) {
            v |= Lowercase;
        }
        if (isdigit(c)) {
            v |= Digit;
        }
        if (isxdigit(c)) {
            v |= HexDigit;
        }
        if (ispunct(c)) {
            v |= Punctuation;
        }
        if (isblank(c)) {
            v |= Whitespace;
        }
        if (iscntrl(c)) {
            v |= Control;
        }
        printf("%i", v);
        if (c != 127) {
            printf(", ");
        }
    }
    printf("}\n");
    return 0;
}

// #include <math.h>
// typedef _Float16 half;
// void _print(char* str, double float64) {
//     half float16 = float64;
//     float float32 = float64;
//     printf("Half: 0x%"PRIx16"\n", *(uint16_t*)(&float16));
//     printf("Single: 0x%"PRIx32"\n", *(uint32_t*)(&float32));
//     printf("Double: 0x%"PRIx64"\n", *(uint64_t*)(&float64));
//     printf("All: 0x%"PRIx16", 0x%"PRIx32", 0x%"PRIx64"\n", *(uint16_t*)(&float16), *(uint32_t*)(&float32), *(uint64_t*)(&float64));
// }
// #define print(x) (_print(#x, (x)))
// int main(void) {
//     print(M_SQRT2);
//     return 0;
// }

// #include <time.h>
// int main(void) {
//     #define ITERATIONS 1000000
//     double start = (double)clock() / (double)CLOCKS_PER_SEC;
//     for (int i = 0; i < ITERATIONS; i++) {
//         typedef double T;
//         typedef int32_t int32;
//         double value = 250;
//         double prev = 0;
//         double out = 1 + value;
//         double term = value;
//         int i = 2;
//         while (out != prev) {
//             prev = out;
//             term *= value / i;
//             i++;
//             out += term;
//         }
//     }
//     double end = (double)clock() / (double)CLOCKS_PER_SEC;
//     double time = end - start;
//     printf("Run %i iterations in %f seconds (%f iterations/second)\n", ITERATIONS, time, ITERATIONS/time);
//     return 0;
// }
