#include <stdint.h>
#include <stdio.h>

uint64_t state = 12345;

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
    FILE *fd = fopen("queries.txt", "w");
    for (long long i = 0; i < 100000; i++) {
        fprintf(fd, "%llu\n", rand_50bit());
    }

    fclose(fd);
}
