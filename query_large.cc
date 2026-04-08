/*
 * trans-cnt.cc
 *
 * Computes the aggregate count of how many times query values appear
 * in the database. Duplicate queries count multiple times. Each integer is assumed to fit in 60 bits, so values are stored as uint64_t.
 *
 * The essential algorithm is as discussed in class.
 * Essentially, Store queries and db in order as key value pairs (i.e. 3 7 4 7 -> 3:1, 4:1, 7:2).
Then, sort query.
"Combine duplicates in the queries as well".
* Then, we can do a single pass through the sorted query and db to get the final answer. If the current query value is less than the current db value, we advance the query. If the current query value is greater than the current db value, we advance the db. If they are equal, we add to the answer the product of their counts (e.g. if query has 7:2 and db has 7:3, then we add 2*3=6 to the answer), and advance both.
* In other words, think of it as [Sort db], [Sort query], [Group db into (value, count)], [Group query into (value, count)], [Intersect the two grouped streams to get the final answer].
 *
 * The program reuses the sorting algorith submitted for HW5 (merge chunks). 
 * The main difference is that instead of writing a final sorted output file, we directly merge the two sorted streams of runs together and count the matches on the fly. This is more efficient since we do not need to write a final sorted file that we do not need, and we avoid building a huge in-memory hash table for counting duplicates.
 * Intuitively, think of it as intermediately genreating the (value, count) pairs for the database and query, and then intersecting those pairs to get the final answer.
 * The result is great storage efficiency, since it fits in the L1.
 *
 * I have copied my external merge sort explaation below:
 * The program uses an external merge sort:
 *
 * First, 
 *   - Read the input file in chunks that fit in memory.
 *   - Parse each ASCII line into a uint64_t.
 *   - Sort the chunk in memory using std::sort.
 *   - Write the sorted chunk to a temporary binary file. This is piped into disk and is called a "run". You may notice temporary files named like run_000000.bin, run_000001.bin, etc.
 *
 * Next, these are merged together into the final output file:
 *   - Open all sorted run files.
 *   - Maintain one buffered reader per run.
 *   - Keep the current smallest value from each run in a min-heap.
 *   - Repeatedly extract the smallest value, write it to the final output,
 *     then refill from that same run.
 *
 * Compile:
 *  g++ -O2 -o query_large query_large.cc
 * Usage:
 *   ./query_large <database> <queries> [chunk_mib]
 *
 * Correctness:
 * I implemented a naive un-optimized un0sorted version to check correctness of small inputs.
 * I initially tried using the given script (generate_db.txt) to test on small inputs of 100k, but ran into issues.
 * Since the numbers are so random, the intersection is often empty.
 *Hence, i made a version which takes in a probability p, and includes random elements from the db with that probability.
 *
 * The results match for 100 iterations, so I assumed correctness.
 *
 *Also, due to storage limitations due to undergraduate permissions, I record data on my own machine.
 *My local machine configurations are (obtainale through `lscpu`): x86_64, 32-bit, 16 cores, 15 GiB RAM, 384 KB L1D, 256 L1I, 10 MiB L2, 16 MiB L3.
#Unfortunately, it seems that the default scripts given also say 0 collision.
//This is not particularly a good metric to test on.
//Hence, I made a simple version that produces queries by randomly deciding (based on the collision ratio) whether to sample a value from the database or generate a fresh random value; when sampling from the database, it selects a random index and repeats that value multiple times so that the average repetition matches the specified frequency.
//I have included the query text in submission.
//You can find it in biased_queries.txt. This is the one that I used to get the results from.
#Here are the results: (42753)
* Chunk size (MiB)    Total cycles  Time
*Default (128)       132424970570   2m55.757s
*64                  137824677956   3m3.123s
*256                 119877150849   2m59.371s
*512                 108203847354  2m46.660s
*32                  147100711874   2m55.028s
*
*You may recall that my HW5 stated that chunk size 64 was the best. 
*However, in this case, it seems that larger chunk sizes perform better.
*One explanation may be that due to the format of the task, there is not a need to actually finish merging the entirety of the database.
*Once the queries finish, the merging can stop.
*In that sense, it may be more efficient to have larger chunk sizes, since that would reduce the number of runs and thus the number of comparisons needed to merge the queries with the database.
*However, this observation is solely due to the format of biased_queries.txt, and in general, relying on the 64MB chunk size is probably the best bet, since it is the most efficient chunk size for sort.
*
*There would be a significant slowdown if the task was instead formatted as "for each query, count how many times it appears in the database", since then we would have to do a binary search for each query in the sorted database, which would be O(Q log D) where Q is the number of queries and D is the number of database entries.
*Plus, it would potentially be difficult in terms of cache efficiency, since we would need to keep the data structure in memory.
*However, an easy way to circumvent this is to read the queries first, then only store the ones we need.
*In that case, since queries is small (100K), it will fit in memory.
 */

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <queue>
#include <stdexcept>
#include <string>
#include <vector>

using std::size_t;

static constexpr std::uint64_t DEFAULT_CHUNK_MIB = 128;
static constexpr size_t RUN_INPUT_BUFFER_ELEMS = 8192;

[[noreturn]] static void fail(const std::string& msg) {
    throw std::runtime_error(msg);
}

//Helpers===========================================================================

/*
 * We use cpuid + rdtsc at the start to serialize before measurement.
 * This helps reduce out-of-order execution effects on the timestamp.
 */
static inline uint64_t rdtsc_start(void) {
    unsigned int lo, hi;
    __asm__ volatile(
        "cpuid\n\t"
        "rdtsc\n\t"
        : "=a"(lo), "=d"(hi)
        : "a"(0)
        : "rbx", "rcx");
    return ((uint64_t)hi << 32) | lo;
}

/*
 * We use rdtscp + cpuid at the end to serialize after measurement.
 * This gives a cleaner cycle interval.
 */
static inline uint64_t rdtsc_end(void) {
    unsigned int lo, hi;
    __asm__ volatile(
        "rdtscp\n\t"
        "mov %%eax, %0\n\t"
        "mov %%edx, %1\n\t"
        "cpuid\n\t"
        : "=r"(lo), "=r"(hi)
        :
        : "rax", "rbx", "rcx", "rdx");
    return ((uint64_t)hi << 32) | lo;
}

//parser for uint64_t that checks for invalid input and trailing characters
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

// Convert chunk size in MiB to number of uint64_t elements that fit in that chunk.
// Example: 128 MiB chunk means 128 * 1024 * 1024 bytes, which is 134,217,728 bytes.
static size_t mib_to_chunk_elems(std::uint64_t mib) {
    std::uint64_t bytes = mib * 1024ULL * 1024ULL;
    size_t elems = static_cast<size_t>(bytes / sizeof(std::uint64_t));
    if (elems == 0) fail("Chunk size too small");
    return elems;
}

// Run generation. A "run" is a sorted chunk of the input that we write to disk. We generate multiple runs and then merge them later. ========

struct RunList {
    std::vector<std::string> paths;
};

// Writes a vector of uint64_t values to a binary file. The file will contain the raw binary representation of the uint64_t values, without any delimiters or text formatting.
//Used to dump output of run generation to disk.
static void write_run_binary(const std::string& path,
                             const std::vector<std::uint64_t>& data) {
    std::ofstream out(path, std::ios::binary);
    if (!out) fail("Failed to open run file for writing: " + path);

    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size() * sizeof(std::uint64_t)));

    if (!out) fail("Failed to write run file: " + path);
}

// Main chunk generation function:
// - Reads the input file line by line, parsing uint64_t values.
// - Accumulates values into a chunk until it reaches the specified size.
// - Sorts the chunk and writes it to a temporary binary file.
static RunList generate_runs(const std::string& input_path,
                             size_t chunk_elems,
                             const std::string& prefix) {
    std::ifstream in(input_path);
    if (!in) fail("Failed to open input file: " + input_path);

    RunList runs;
    std::vector<std::uint64_t> chunk;
    chunk.reserve(chunk_elems);

    std::string line;
    size_t run_id = 0;

    while (true) {
        chunk.clear();

        while (chunk.size() < chunk_elems && std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            chunk.push_back(parse_u64_strict(line));
        }

        if (chunk.empty()) break;

        std::sort(chunk.begin(), chunk.end());

        char path_buf[128];
        std::snprintf(path_buf, sizeof(path_buf), "%s_%06zu.bin",
                      prefix.c_str(), run_id++);
        std::string run_path(path_buf);

        write_run_binary(run_path, chunk);
        runs.paths.push_back(run_path);

        if (in.eof()) break;
    }

    return runs;
}

// Buffered reader for a run file. It reads uint64_t values from the binary run file
class RunReader {
public:
    explicit RunReader(const std::string& path)
        : in_(path, std::ios::binary),
          buffer_(RUN_INPUT_BUFFER_ELEMS),
          filled_(0),
          pos_(0),
          exhausted_(false) {
        if (!in_) fail("Failed to open run file for reading: " + path);
    }

    bool next(std::uint64_t& out) {
        if (exhausted_) return false;

        if (pos_ >= filled_) {
            refill();
            if (exhausted_) return false;
        }

        out = buffer_[pos_++];
        return true;
    }

private:
 // Refill the buffer from the run file. Updates filled_ and pos_.
    void refill() {
        in_.read(reinterpret_cast<char*>(buffer_.data()),
                 static_cast<std::streamsize>(buffer_.size() * sizeof(std::uint64_t)));
        std::streamsize bytes = in_.gcount();

        if (bytes < 0) fail("Failed while reading run file");

        filled_ = static_cast<size_t>(
            bytes / static_cast<std::streamsize>(sizeof(std::uint64_t)));
        pos_ = 0;

        if (filled_ == 0) exhausted_ = true;
    }

    std::ifstream in_;
    std::vector<std::uint64_t> buffer_;
    size_t filled_;
    size_t pos_;
    bool exhausted_;
};


//We no longer need an output bugger, since we are not dumping an output file. Skip the output buffer part 

//Node in the min-heap for merge
struct HeapNode {
    std::uint64_t value;
    size_t run_idx;

    bool operator>(const HeapNode& other) const {
        if (value != other.value) return value > other.value;
        return run_idx > other.run_idx;
    }
};

// Merged stream for merging multiple runs. This is a replacement for output dump.
//Faster, no longer writes to disk.
class MergedStream {
public:
    explicit MergedStream(const RunList& runs) {
        readers_.reserve(runs.paths.size());
        for (const auto& path : runs.paths) {
            readers_.emplace_back(path);
        }

        for (size_t i = 0; i < readers_.size(); ++i) {
            std::uint64_t v;
            if (readers_[i].next(v)) {
                min_heap_.push(HeapNode{v, i});
            }
        }
    }

    bool next_value(std::uint64_t& out) {
        if (min_heap_.empty()) return false;

        HeapNode cur = min_heap_.top();
        min_heap_.pop();
        out = cur.value;

        std::uint64_t next_v;
        if (readers_[cur.run_idx].next(next_v)) {
            min_heap_.push(HeapNode{next_v, cur.run_idx});
        }
        return true;
    }

//cllassses for grouping and counting duplicates on the fly, and for intersecting the grouped streams, are below. We no longer need the output buffer class, since we are not dumping an output file. Skip the output buffer part.
private:
    std::vector<RunReader> readers_;
    std::priority_queue<HeapNode, std::vector<HeapNode>, std::greater<HeapNode>> min_heap_;
};


//Group represents the data structure of occurences. Like a hashmap, basically.
//Reads sorted values
//Groups identical consecutive values
//Outputs value, pairs
struct Group {
    std::uint64_t value;
    std::uint64_t count;
};

class GroupedStream {
public:
    explicit GroupedStream(const RunList& runs)
        : merged_(runs), has_saved_(false), saved_(0) {}

    // Get the next group of identical values. Returns false if no more groups.
    bool next_group(Group& g) {
        std::uint64_t v;
        if (has_saved_) {
            v = saved_;
            has_saved_ = false;
        } else {
            if (!merged_.next_value(v)) return false;
        }

        g.value = v;
        g.count = 1;

        std::uint64_t x;
        while (merged_.next_value(x)) {
            if (x == g.value) {
                ++g.count;
            } else {
                saved_ = x;
                has_saved_ = true;
                break;
            }
        }

        return true;
    }

private:
    MergedStream merged_;
    bool has_saved_;
    std::uint64_t saved_;
};

//cleanup code
static void delete_runs(const RunList& runs) {
    for (const auto& path : runs.paths) {
        if (std::remove(path.c_str()) != 0) {
            std::cerr << "Warning: could not remove temp file " << path << "\n";
        }
    }
}

static std::uint64_t transaction_count(const RunList& db_runs,
                                       const RunList& q_runs) {
    GroupedStream db(db_runs);
    GroupedStream qs(q_runs);

    Group db_g{}, q_g{};
    bool db_ok = db.next_group(db_g);
    bool q_ok  = qs.next_group(q_g);

    std::uint64_t total = 0;

    //main matching code. Intersect the two grouped streams:
    // basically implemented as a sliding window.
    while (db_ok && q_ok) {
        if (db_g.value < q_g.value) {
            db_ok = db.next_group(db_g);
        } else if (db_g.value > q_g.value) {
            q_ok = qs.next_group(q_g);
        } else {
            // Matching value: every query occurrence contributes one full db count.
            if (db_g.count != 0 &&
                q_g.count > std::numeric_limits<std::uint64_t>::max() / db_g.count) {
                fail("Aggregate total overflowed uint64_t");
            }
            total += db_g.count * q_g.count;
            db_ok = db.next_group(db_g);
            q_ok  = qs.next_group(q_g);
        }
    }

    return total;
}

int main(int argc, char** argv) {
    try {
        if (argc != 3 && argc != 4) {
            std::cerr << "Usage: " << argv[0] << " <database> <queries> [chunk_mib]\n";
            return EXIT_FAILURE;
        }

        const std::string db_path = argv[1];
        const std::string q_path  = argv[2];

        std::uint64_t chunk_mib = DEFAULT_CHUNK_MIB;
        if (argc == 4) {
            chunk_mib = std::stoull(argv[3]);
            if (chunk_mib == 0) fail("chunk_mib must be positive");
        }

        std::cerr << "Generating runs using chunk size: " << chunk_mib << " MiB\n";

        size_t chunk_elems = mib_to_chunk_elems(chunk_mib);

        RunList db_runs = generate_runs(db_path, chunk_elems, "db_run");
        std::cerr << "Database runs generated: " << db_runs.paths.size() << "\n";
        RunList q_runs  = generate_runs(q_path,  chunk_elems, "q_run");
        std::cerr << "Query runs generated: " << q_runs.paths.size() << "\n";

        //begin cycle timing here
        uint64_t c0 = rdtsc_start();
        std::uint64_t ans = transaction_count(db_runs, q_runs);
        uint64_t c1 = rdtsc_end();
        //end cycle timing here
        std::cout << ans << "\n";

        uint64_t total_cycles = c1 - c0;
        std::cerr << "Total cycles: " << total_cycles << "\n";

        delete_runs(db_runs);
        delete_runs(q_runs);
        return EXIT_SUCCESS;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }
}
