#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static uint64_t state;

static uint64_t xorshift64star() {
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    return state * 2685821657736338717ULL;
}

static uint64_t rand_50bit() {
    return xorshift64star() & ((1ULL << 50) - 1);
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <seed> <count> <iterations>\n", argv[0]);
        return 1;
    }

    uint64_t base_seed = strtoull(argv[1], NULL, 10);
    long long count = atoll(argv[2]);
    int iterations = atoi(argv[3]);

    if (count < 0 || iterations <= 0) {
        fprintf(stderr, "Invalid arguments\n");
        return 1;
    }

    for (int it = 0; it < iterations; it++) {
        char fname[64];
        snprintf(fname, sizeof(fname), "db_%d.txt", it);

        FILE *fd = fopen(fname, "w");
        if (!fd) {
            perror("fopen");
            return 1;
        }

        // use a distinct seed per iteration
        state = base_seed + (uint64_t)it;

        for (long long i = 0; i < count; i++) {
            fprintf(fd, "%llu\n", (unsigned long long)rand_50bit());
        }

        fclose(fd);
    }

    return 0;
}
