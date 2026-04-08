#include <stdint.h>
#include <stdio.h>

uint64_t state = 54321;

uint64_t xorshift64star() {
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    return state * 2685821657736338717ULL;
}

uint64_t rand_50bit() {
    return xorshift64star() & ((1ULL << 50) - 1);
}

int main() {
    FILE *fd = fopen("db.txt", "w");

    long long billion = 1LL << 30;
    ////testing purposes
    //long long billion = 10000;
    for (long long i = 0; i < billion; i++) {
        fprintf(fd, "%llu\n", rand_50bit());
    }

    fclose(fd);
}
