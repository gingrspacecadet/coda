
#include <inttypes.h>
#include <stdio.h>

#include <math.h>
typedef _Float16 half;
void _print(char* str, double float64) {
    half float16 = float64;
    float float32 = float64;
    printf("Half: 0x%"PRIx16"\n", *(uint16_t*)(&float16));
    printf("Single: 0x%"PRIx32"\n", *(uint32_t*)(&float32));
    printf("Double: 0x%"PRIx64"\n", *(uint64_t*)(&float64));
    printf("All: 0x%"PRIx16", 0x%"PRIx32", 0x%"PRIx64"\n", *(uint16_t*)(&float16), *(uint32_t*)(&float32), *(uint64_t*)(&float64));
}
#define print(x) (_print(#x, (x)))
int main(void) {
    print(M_SQRT2);
    return 0;
}

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
