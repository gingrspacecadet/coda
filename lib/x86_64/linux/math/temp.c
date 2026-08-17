
#include <inttypes.h>
#include <stdio.h>
#include <time.h>
// #include <math.h>

typedef double T;
typedef int32_t int32;
#define fn


int main(void) {
    // #define print(x) {double float64value = x; printf(#x" = 0x%"PRIx64"\n", *(uint64_t*)(&float64value));}
    // print(M_LN2);
    #define ITERATIONS 1000000
    double start = (double)clock() / (double)CLOCKS_PER_SEC;
    for (int i = 0; i < ITERATIONS; i++) {
        typedef double T;
        typedef int32_t int32;
        double value = 250;
        double prev = 0;
        double out = 1 + value;
        double term = value;
        int i = 2;
        while (out != prev) {
            prev = out;
            term *= value / i;
            i++;
            out += term;
        }
    }
    double end = (double)clock() / (double)CLOCKS_PER_SEC;
    double time = end - start;
    printf("Run %i iterations in %f seconds (%f iterations/second)\n", ITERATIONS, time, ITERATIONS/time);
    return 0;
}
