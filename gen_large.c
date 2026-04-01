#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define TOTAL_LINES 1000000000ULL
#define BUFFER_SIZE (1 << 20)  // 1 MB buffer

// xorshift64*
static uint64_t x = 88172645463325252ULL;

static inline uint64_t next_rand() {
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    return x * 2685821657736338717ULL;
}

// Convert uint64 to ASCII, return length
static inline int u64_to_str(uint64_t val, char *buf) {
    char tmp[32];
    int i = 0;

    // output digits in reverse
    do {
        tmp[i++] = '0' + (val % 10);
        val /= 10;
    } while (val);

    // Reverse into output buffer
    int len = i;
    for (int j = 0; j < len; j++) {
        buf[j] = tmp[len - j - 1];
    }

    return len;
}

int main() {
    FILE *f = fopen("hw5_test.txt", "w");
    if (!f) {
        perror("fopen");
        return 1;
    }

    char *buffer = malloc(BUFFER_SIZE);
    if (!buffer) {
        perror("malloc");
        return 1;
    }

    size_t pos = 0;

    for (uint64_t i = 0; i < TOTAL_LINES; i++) {
        uint64_t val = next_rand() & ((1ULL << 60) - 1);

        char num[32];
        int len = u64_to_str(val, num);

        if (pos + len + 1 >= BUFFER_SIZE) {
            fwrite(buffer, 1, pos, f);
            pos = 0;
        }

        for (int j = 0; j < len; j++) {
            buffer[pos++] = num[j];
        }

        buffer[pos++] = '\n';
    }

    // Final flush
    if (pos > 0) {
        fwrite(buffer, 1, pos, f);
    }

    free(buffer);
    fclose(f);
}
