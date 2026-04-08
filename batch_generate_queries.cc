#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <fstream>
#include <string>
#include <iostream>

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

static double rand_unit() {
    // value in [0,1)
    return (xorshift64star() >> 11) * (1.0 / 9007199254740992.0);
}

static std::vector<uint64_t> load_db(const char *fname, long long expected_count) {
    std::vector<uint64_t> db;
    db.reserve((size_t)expected_count);

    std::ifstream in(fname);
    if (!in) {
        std::cerr << "Failed to open " << fname << "\n";
        exit(1);
    }

    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        db.push_back(std::stoull(line));
    }

    return db;
}

int main(int argc, char **argv) {
    if (argc != 6) {
        fprintf(stderr,
                "Usage: %s <seed> <db_count> <query_count> <collision_ratio> <iterations>\n",
                argv[0]);
        return 1;
    }

    uint64_t base_seed = strtoull(argv[1], NULL, 10);
    long long db_count = atoll(argv[2]);
    long long query_count = atoll(argv[3]);
    double collision_ratio = atof(argv[4]);
    int iterations = atoi(argv[5]);

    if (db_count < 0 || query_count < 0 || iterations <= 0) {
        fprintf(stderr, "Invalid count/iteration arguments\n");
        return 1;
    }

    if (collision_ratio < 0.0 || collision_ratio > 1.0) {
        fprintf(stderr, "collision_ratio must be between 0.0 and 1.0\n");
        return 1;
    }

    for (int it = 0; it < iterations; it++) {
        char db_name[64];
        char q_name[64];

        snprintf(db_name, sizeof(db_name), "db_%d.txt", it);
        snprintf(q_name, sizeof(q_name), "queries_%d.txt", it);

        std::vector<uint64_t> db = load_db(db_name, db_count);
        if (db.empty()) {
            std::cerr << "Database file " << db_name << " is empty\n";
            return 1;
        }

        FILE *fd = fopen(q_name, "w");
        if (!fd) {
            perror("fopen");
            return 1;
        }

        // distinct seed per iteration
        state = base_seed + (uint64_t)it;

        long long from_db = 0;
        long long fresh = 0;

        for (long long i = 0; i < query_count; i++) {
            uint64_t q;

            if (rand_unit() < collision_ratio) {
                uint64_t idx = xorshift64star() % db.size();
                q = db[(size_t)idx];
                from_db++;
            } else {
                q = rand_50bit();
                fresh++;
            }

            fprintf(fd, "%llu\n", (unsigned long long)q);
        }

        fclose(fd);

        printf("iteration %d: wrote %s (from_db=%lld, fresh=%lld)\n",
               it, q_name, from_db, fresh);
    }

    return 0;
}
