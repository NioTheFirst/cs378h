#include <stdint.h>
#include <stdio.h>
#include <vector>
#include <fstream>
#include <string>
#include <iostream>

uint64_t state = 123;

uint64_t xorshift64star() {
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    return state * 2685821657736338717ULL;
}

uint64_t rand_50bit() {
    return xorshift64star() & ((1ULL << 50) - 1);
}

bool coin_flip() {
    return (xorshift64star() & 1ULL);
}

int main() {
    // -----------------------------
    // Step 1: Load database into memory
    // -----------------------------
    std::vector<uint64_t> db;
    std::ifstream in("db.txt");

    if (!in) {
        std::cerr << "Failed to open db.txt\n";
        return 1;
    }

    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        db.push_back(std::stoull(line));
    }

    if (db.empty()) {
        std::cerr << "Database is empty\n";
        return 1;
    }

    // -----------------------------
    // Step 2: Generate queries
    // -----------------------------
    FILE *fd = fopen("queries.txt", "w");

    for (long long i = 0; i < 100000; i++) {
        uint64_t q;

        if (coin_flip()) {
            // 50%: pick random element from DB
            uint64_t idx = xorshift64star() % db.size();
            q = db[idx];
        } else {
            // 50%: fresh random
            q = rand_50bit();
        }

        fprintf(fd, "%llu\n", q);
    }

    fclose(fd);
}
