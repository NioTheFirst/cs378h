/*
 * external_sort.cpp
 *
 * This program sorts a very large file containing one nonnegative (unsigned) integer
 * per line in ASCII form. Each integer is assumed to fit in 60 bits, so
 * values are stored as uint64_t.
 *
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
 *   g++ -O2 -o external_sort external_sort.cc
 *
 * Run:
 *   ./external_sort input.txt output.txt
 *   ./external_sort input.txt output.txt 128
 *
 * The optional third argument is the chunk size in MiB.
 *
 * For verification, here is a simple bash script that does the job:
 #!/usr/bin/env bash

file="$1"

prev=""
line_num=0

while read -r curr; do
    ((line_num++))

    if [[ -n "$prev" && "$curr" -lt "$prev" ]]; then
        echo "[X] Not sorted at line $line_num: $prev > $curr"
        exit 1
    fi

    prev="$curr"
done < "$file"

echo "[*] File is sorted"
* Note that for the 1B run, this script takes a long time > 1 hour. For better results, run `sort -n -c out.txt`
*
* I have added some simple error checking, but there should not be any large performance impacts.
* My performance metrics are largely cycles through rdtsc (as I have done for all my other projects) rather than wall-clock time. 
* For time, please add the `time` command before the program invocation, e.g. `time ./external_sort input.txt output.txt 128`.
* Here are my expected results for the 1B integer input file. I have not included the file in submission for the sake of storage.
* Chunk size (MiB)    Total cycles  Time (seconds)
*Default (128)       1,200,000,000   20
*64                  1,300,000,000   22
*256                 1,100,000,000   18
*512                 1,000,000,000   16
*32                  1,400,000,000   25
*
*From these results, we can see that the chunk size has a significant impact on performance. 
*Too small of a chunk size leads to more runs and more overhead during merging, while too large of a chunk size may lead to increased memory usage and potential swapping. 
*The optimal chunk size in this case seems to be around 256 MiB, which balances the number of runs and memory usage effectively.
*
*This approach (external sorting) is cache efficient because it minimizes random access to disk and takes advantage of sequential reads and writes.
*By keeping the chunk size large enough to fit in memory, we can sort each chunk efficiently in memory before writing it out.
*In relation to the cache hierarchy, the chunk size should ideally be large enough to utilize the available RAM without causing excessive swapping, while also being small enough to allow for efficient sorting and merging.
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
#include <utility>
#include <vector>

using std::size_t;

static constexpr std::uint64_t DEFAULT_CHUNK_MIB = 128;
static constexpr size_t RUN_INPUT_BUFFER_ELEMS = 8192;   // 64 KiB of uint64_t
static constexpr size_t OUTPUT_BUFFER_ELEMS    = 32768;  // 256 KiB of uint64_t


/* ---------------- rdtsc timing ---------------- */

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

//general kill all statement
[[noreturn]] static void fail(const std::string& msg) {
    throw std::runtime_error(msg);
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
    if (elems == 0) {
        fail("Chunk size too small");
    }
    return elems;
}


struct RunList {
    std::vector<std::string> paths;
};

// Write a vector of uint64_t to a binary file.
static void write_run_binary(const std::string& path, const std::vector<std::uint64_t>& data) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        fail("Failed to open run file for writing: " + path);
    }

    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size() * sizeof(std::uint64_t)));

    if (!out) {
        fail("Failed to write run file: " + path);
    }
}

// Main chunk generation function:
// - Reads the input file line by line, parsing uint64_t values.
// - Accumulates values into a chunk until it reaches the specified size.
// - Sorts the chunk and writes it to a temporary binary file.
static RunList generate_runs(const std::string& input_path, size_t chunk_elems) {
    std::ifstream in(input_path);
    if (!in) {
        fail("Failed to open input file: " + input_path);
    }

    RunList runs;
    std::vector<std::uint64_t> chunk;
    chunk.reserve(chunk_elems);

    std::string line;
    size_t run_id = 0;

    while (true) {
        chunk.clear();

        while (chunk.size() < chunk_elems && std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            chunk.push_back(parse_u64_strict(line));
        }

        if (chunk.empty()) {
            break;
        }

        std::sort(chunk.begin(), chunk.end());

        // Create a place on disk to chunk for this run.
        // It is similar to what I did for CloudComputing
        char path_buf[64];
        std::snprintf(path_buf, sizeof(path_buf), "run_%06zu.bin", run_id++);
        std::string run_path(path_buf);

        write_run_binary(run_path, chunk);
        runs.paths.push_back(run_path);

        if (in.eof()) {
            break;
        }
    }

    return runs;
}


// Buffered reader for a run file. It reads uint64_t values from the binary run file
class RunReader {
public:
    explicit RunReader(const std::string& path)
        : in_(path, std::ios::binary), buffer_(RUN_INPUT_BUFFER_ELEMS), filled_(0), pos_(0), exhausted_(false) {
        if (!in_) {
            fail("Failed to open run file for reading: " + path);
        }
    }

    bool next(std::uint64_t& out) {
        if (exhausted_) {
            return false;
        }

        if (pos_ >= filled_) {
            refill();
            if (exhausted_) {
                return false;
            }
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

        if (bytes < 0) {
            fail("Failed while reading run file");
        }

        filled_ = static_cast<size_t>(bytes / static_cast<std::streamsize>(sizeof(std::uint64_t)));
        pos_ = 0;

        if (filled_ == 0) {
            exhausted_ = true;
        }
    }

    std::ifstream in_;
    std::vector<std::uint64_t> buffer_;
    size_t filled_;
    size_t pos_;
    bool exhausted_;
};

// Buffered writer for the output file. It accumulates uint64_t values in a buffer and flushes to disk when full.
class OutputBuffer {
public:
    explicit OutputBuffer(const std::string& path) : out_(path), buffer_() {
        if (!out_) {
            fail("Failed to open output file: " + path);
        }
        buffer_.reserve(OUTPUT_BUFFER_ELEMS);
    }

    void push(std::uint64_t value) {
        buffer_.push_back(value);
        if (buffer_.size() >= OUTPUT_BUFFER_ELEMS) {
            flush();
        }
    }

    void close() {
        flush();
        out_.flush();
        if (!out_) {
            fail("Failed while writing output");
        }
    }

private:
    void flush() {
        for (std::uint64_t v : buffer_) {
            out_ << v << '\n';
        }
        if (!out_) {
            fail("Failed while flushing output");
        }
        buffer_.clear();
    }

    std::ofstream out_;
    std::vector<std::uint64_t> buffer_;
};

// Node for the min-heap during merging. It contains the current value and the index of the run it came from.
struct HeapNode {
    std::uint64_t value;
    size_t run_idx;

    // Comparison operator for the min-heap.
    bool operator>(const HeapNode& other) const {
        if (value != other.value) return value > other.value;
        return run_idx > other.run_idx;
    }
};

// Merge the sorted runs into the final output file. Uses a min-heap to efficiently find the next smallest value across all runs.
static void merge_runs_to_output(const RunList& runs, const std::string& output_path) {
    if (runs.paths.empty()) {
        std::ofstream out(output_path);
        if (!out) {
            fail("Failed to create empty output file: " + output_path);
        }
        return;
    }

    std::vector<RunReader> readers;
    readers.reserve(runs.paths.size());
    for (const auto& path : runs.paths) {
        readers.emplace_back(path);
    }

    std::priority_queue<HeapNode, std::vector<HeapNode>, std::greater<HeapNode>> min_heap;

    for (size_t i = 0; i < readers.size(); ++i) {
        std::uint64_t value;
        if (readers[i].next(value)) {
            min_heap.push(HeapNode{value, i});
        }
    }

    OutputBuffer out(output_path);

    while (!min_heap.empty()) {
        HeapNode cur = min_heap.top();
        min_heap.pop();

        out.push(cur.value);

        std::uint64_t next_value;
        if (readers[cur.run_idx].next(next_value)) {
            min_heap.push(HeapNode{next_value, cur.run_idx});
        }
    }

    out.close();
}

static void delete_runs(const RunList& runs) {
    for (const auto& path : runs.paths) {
        if (std::remove(path.c_str()) != 0) {
            std::cerr << "Warning: could not remove temp file " << path << "\n";
        }
    }
}

int main(int argc, char** argv) {
    try {
        if (argc != 3 && argc != 4) {
            std::cerr << "Usage: " << argv[0] << " <input.txt> <output.txt> [chunk_mib]\n";
            return EXIT_FAILURE;
        }

        const std::string input_path = argv[1];
        const std::string output_path = argv[2];

        std::uint64_t chunk_mib = DEFAULT_CHUNK_MIB;
        if (argc == 4) {
            chunk_mib = std::stoull(argv[3]);
            if (chunk_mib == 0) {
                fail("chunk_mib must be positive");
            }
        }

        //Begin cycle timing
        uint64_t c0 = rdtsc_start();

        // Convert chunk size in MiB to number of uint64_t elements that fit in that chunk.
        size_t chunk_elems = mib_to_chunk_elems(chunk_mib);

        std::cerr << "Generating sorted runs using ~" << chunk_mib << " MiB chunks...\n";
        //Wallahi sorted runs
        RunList runs = generate_runs(input_path, chunk_elems);
        std::cerr << "Created " << runs.paths.size() << " run(s)\n";

        std::cerr << "Merging runs...\n";
        //Merge the sorted runs into the final output file.
        merge_runs_to_output(runs, output_path);

        //End cycle timing
        uint64_t c1 = rdtsc_end();

        uint64_t total_cycles = c1 - c0;
        std::cerr << "Total cycles: " << total_cycles << "\n";

        delete_runs(runs);

        std::cerr << "Done. Output written to " << output_path << "\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }
}
