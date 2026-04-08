/*
 * naive_trans_cnt.cc
 *
 * Slow correctness checker for the transaction-count problem.
 *
 * Usage:
 *   ./naive_trans_cnt <database> <queries>
 *
 * Idea:
 *   For each query value q, scan the full database and count how many
 *   times q appears. Add that to the aggregate answer.
 *
 * This is O(|database| * |queries|), so it is only suitable for small tests.
 */

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

[[noreturn]] static void fail(const std::string& msg) {
    throw std::runtime_error(msg);
}

static std::uint64_t parse_u64_strict(const std::string& s) {
    size_t idx = 0;
    unsigned long long value = 0;

    try {
        value = std::stoull(s, &idx, 10);
    } catch (const std::exception&) {
        fail("Invalid integer line: \"" + s + "\"");
    }

    while (idx < s.size() &&
           (s[idx] == ' ' || s[idx] == '\t' || s[idx] == '\r' || s[idx] == '\n')) {
        ++idx;
    }

    if (idx != s.size()) {
        fail("Invalid trailing characters in line: \"" + s + "\"");
    }

    return static_cast<std::uint64_t>(value);
}

static std::vector<std::uint64_t> read_values(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        fail("Failed to open input file: " + path);
    }

    std::vector<std::uint64_t> vals;
    std::string line;

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        vals.push_back(parse_u64_strict(line));
    }

    return vals;
}

static std::uint64_t naive_transaction_count(
    const std::vector<std::uint64_t>& database,
    const std::vector<std::uint64_t>& queries) {

    std::uint64_t total = 0;

    for (std::uint64_t q : queries) {
        for (std::uint64_t x : database) {
            if (x == q) {
                ++total;
            }
        }
    }

    return total;
}

int main(int argc, char** argv) {
    try {
        if (argc != 3) {
            std::cerr << "Usage: " << argv[0] << " <database> <queries>\n";
            return EXIT_FAILURE;
        }

        const std::string db_path = argv[1];
        const std::string q_path  = argv[2];

        std::vector<std::uint64_t> database = read_values(db_path);
        std::vector<std::uint64_t> queries  = read_values(q_path);

        std::uint64_t ans = naive_transaction_count(database, queries);
        std::cout << ans << "\n";

        return EXIT_SUCCESS;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }
}
