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
    return (xorshift64star() >> 11) * (1.0 / 9007199254740992.0);
}

static long long sample_repeat_count(double avg_freq) {
    if (avg_freq <= 1.0) return 1;

    long long base = (long long)avg_freq;
    double frac = avg_freq - (double)base;

    if (frac == 0.0) return base;
    return (rand_unit() < frac) ? (base + 1) : base;
}

static std::vector<uint64_t> load_db(const char *fname) {
    std::vector<uint64_t> db;
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
    if (argc != 7) {
        fprintf(stderr,
                "Usage: %s <seed> <db_file> <query_count> <collision_ratio> <avg_repeat_freq> <out_file>\n",
                argv[0]);
        return 1;
    }

    uint64_t seed = strtoull(argv[1], NULL, 10);
    const char *db_file = argv[2];
    long long query_count = atoll(argv[3]);
    double collision_ratio = atof(argv[4]);
    double avg_repeat_freq = atof(argv[5]);
    const char *out_file = argv[6];

    if (query_count < 0 || collision_ratio < 0.0 || collision_ratio > 1.0 || avg_repeat_freq < 1.0) {
        fprintf(stderr, "Invalid arguments\n");
        return 1;
    }

    std::vector<uint64_t> db = load_db(db_file);
    if (db.empty()) {
        fprintf(stderr, "Database is empty\n");
        return 1;
    }

    state = seed;

    FILE *fd = fopen(out_file, "w");
    if (!fd) {
        perror("fopen");
        return 1;
    }

    long long written = 0;
    long long db_values_chosen = 0;
    long long db_query_lines = 0;
    long long fresh_query_lines = 0;

    while (written < query_count) {
        if (rand_unit() < collision_ratio) {
            uint64_t idx = xorshift64star() % db.size();
            uint64_t q = db[(size_t)idx];
            long long reps = sample_repeat_count(avg_repeat_freq);
            db_values_chosen++;

            for (long long k = 0; k < reps && written < query_count; k++) {
                fprintf(fd, "%llu\n", (unsigned long long)q);
                written++;
                db_query_lines++;
            }
        } else {
            fprintf(fd, "%llu\n", (unsigned long long)rand_50bit());
            written++;
            fresh_query_lines++;
        }
    }

    fclose(fd);

    printf("db_values_chosen=%lld, db_query_lines=%lld, fresh_query_lines=%lld, realized_avg=%.3f\n",
           db_values_chosen,
           db_query_lines,
           fresh_query_lines,
           db_values_chosen ? ((double)db_query_lines / (double)db_values_chosen) : 0.0);

    return 0;
}
