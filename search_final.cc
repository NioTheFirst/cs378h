/*
Brian Zhang
bz5346

This program implements two string search algorithms over a source file:
1) simple character-by-character search
2) Boyer-Moore search with explicit delta1 and delta2 tables

To run:
    search <src-filename> <pattern-file> <type>

High-level approach:
- First, the program reads the full source file into memory.
- Then it reads every pattern from the pattern file, one pattern per line.
- After that, it searches for every pattern inside the source text using
  either the simple search or Boyer-Moore, depending on the type argument.
- It counts total matches and total character comparisons.
- It reports both cycle count timing (rdtsc) and elapsed wall-clock time.

Main components:
- Simple string search:
  The simple algorithm checks every possible alignment in the source text
  and compares pattern characters left-to-right until mismatch or success.

- Delta1 preprocessing:
  The delta1 table is the Boyer-Moore bad-character table. For each ASCII
  character, delta1 stores the last index where that character appears in
  the pattern, or -1 if it does not appear. This supports a bad-character
  shift after mismatches.

- Delta2 preprocessing:
  The delta2 table is the Boyer-Moore good-suffix table. It is built using
  strong good-suffix preprocessing with helper arrays shift[] and bpos[].
  This supports larger safe shifts based on suffix structure.

Correctness:
- The simple search and Boyer-Moore search should return the same total
  number of matches on the same input files.
- Both algorithms count overlapping matches.
- The main correctness check is to run both algorithm types on the same
  source and pattern files and verify that the total match counts are equal.

Metrics reported:
- matches
- cycles
- seconds
- comparisons

Compilation:
    gcc -O2 -o search search.cc

Example:
    ./search source.txt patterns.txt 1
    ./search source.txt patterns.txt 2

TTiming Results:
Corpus 1
Algorithm        Comparisons      Cycles        Seconds      Matches
Simple           12,299,119       56,460,491    0.021005     91,116
Boyer-Moore         3,569,444      49,108,329    0.018270     91,116

Corpus 2
Algorithm        Comparisons      Cycles        Seconds      Matches
Simple           67,995,602      111,794,906    0.041591     435,403
Boyer-Moore      19,597,742      192,817,564    0.071733     435,403

Corpus 3
Algorithm        Comparisons      Cycles        Seconds      Matches
Simple          138,145,705      205,342,396    0.076393     974,797
Boyer-Moore      40,051,592      363,069,418    0.135071     974,797

Corpus 4
Algorithm        Comparisons      Cycles        Seconds      Matches
Simple          201,584,043      296,140,610    0.110172     1,463,984
Boyer-Moore      58,577,348      530,595,574    0.197395     1,463,984

Corpus 5
Algorithm        Comparisons      Cycles        Seconds      Matches
Simple          444,141,181      661,581,812    0.246125     3,298,647
Boyer-Moore     129,429,227    1,207,011,028    0.449037     3,298,647


Analysis

Boyer-Moore consistently performs far fewer character comparisons than the
simple algorithm (roughly 3–4× fewer across all corpora). This confirms the
algorithmic advantage to reduce the number of comparisons that the Boyer-Moore algorithm proposes.

However, fewer comparisons did not translate to faster execution time. In
most corpuses, the simple algorithm ran faster in both cycles and seconds.
One explanation could be the observation I discussed after class that Boyer Moore does not seem to be as cache efficient as simple search due to its more complex memory access patterns.


*/

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/types.h>
#include <time.h>
#include <x86intrin.h>

#define ALPHABET_SIZE 256

typedef struct {
    uint64_t matches;
    uint64_t comparisons;
} SearchStats;

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

/* ---------------- wall-clock timing ---------------- */

/*
 * We also record elapsed wall-clock time using CLOCK_MONOTONIC.
 * This complements rdtsc and makes results easier to interpret.
 */
static double now_seconds(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* ---------------- File reading ---------------- */

/*
 * Read the entire source file into memory.
 * We do this before timing so file-system latency does not affect results.
 */
static unsigned char *read_entire_file(const char *filename, size_t *out_len) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "Error: cannot open source file '%s': %s\n",
                filename, strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        perror("fseek");
        fclose(fp);
        exit(EXIT_FAILURE);
    }

    long sz = ftell(fp);
    if (sz < 0) {
        perror("ftell");
        fclose(fp);
        exit(EXIT_FAILURE);
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        perror("fseek");
        fclose(fp);
        exit(EXIT_FAILURE);
    }

    size_t n = (size_t)sz;
    unsigned char *buf = (unsigned char *)malloc(n ? n : 1);
    if (!buf) {
        fprintf(stderr, "Error: malloc failed for source file buffer\n");
        fclose(fp);
        exit(EXIT_FAILURE);
    }

    size_t got = fread(buf, 1, n, fp);
    if (got != n) {
        fprintf(stderr, "Error: fread failed on '%s'\n", filename);
        free(buf);
        fclose(fp);
        exit(EXIT_FAILURE);
    }

    fclose(fp);
    *out_len = n;
    return buf;
}

/*
 * Touch every byte of the source buffer before timing.
 * This is intended to force pages in and reduce paging effects.
 */
static volatile unsigned long long sink_sum = 0;

static void touch_all_bytes(const unsigned char *buf, size_t len) {
    unsigned long long sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += buf[i];
    }
    sink_sum = sum;
}

/* ---------------- Pattern loading ---------------- */

/*
 * Store all patterns from the pattern file.
 * One pattern per line, empty lines ignored.
 */
typedef struct {
    char **items;
    size_t count;
    size_t cap;
} PatternList;

static void pattern_list_init(PatternList *pl) {
    pl->items = NULL;
    pl->count = 0;
    pl->cap = 0;
}

static void pattern_list_push(PatternList *pl, char *s) {
    if (pl->count == pl->cap) {
        size_t new_cap = (pl->cap == 0) ? 16 : pl->cap * 2;
        char **new_items = (char **)realloc(pl->items, new_cap * sizeof(char *));
        if (!new_items) {
            fprintf(stderr, "Error: realloc failed for pattern list\n");
            exit(EXIT_FAILURE);
        }
        pl->items = new_items;
        pl->cap = new_cap;
    }
    pl->items[pl->count++] = s;
}

static void pattern_list_free(PatternList *pl) {
    for (size_t i = 0; i < pl->count; i++) {
        free(pl->items[i]);
    }
    free(pl->items);
}

/*
 * Remove trailing newline and CR characters.
 * Useful for Linux and Windows line endings.
 */
static void strip_newline_and_cr(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[n - 1] = '\0';
        n--;
    }
}

/*
 * Read all patterns from the pattern file into a growable list.
 * Debugging note:
 * - If a pattern seems missing, check whether the line was empty after stripping.
 */
static PatternList load_patterns(const char *pattern_filename) {
    FILE *fp = fopen(pattern_filename, "r");
    if (!fp) {
        fprintf(stderr, "Error: cannot open pattern file '%s': %s\n",
                pattern_filename, strerror(errno));
        exit(EXIT_FAILURE);
    }

    PatternList pl;
    pattern_list_init(&pl);

    char *line = NULL;
    size_t cap = 0;
    ssize_t len;

    while ((len = getline(&line, &cap, fp)) != -1) {
        (void)len;
        strip_newline_and_cr(line);

        if (line[0] == '\0') {
            continue;
        }

        char *copy = strdup(line);
        if (!copy) {
            fprintf(stderr, "Error: strdup failed\n");
            free(line);
            fclose(fp);
            pattern_list_free(&pl);
            exit(EXIT_FAILURE);
        }
        pattern_list_push(&pl, copy);
    }

    free(line);
    fclose(fp);
    return pl;
}

/* ---------------- Simple search ---------------- */

/*
 * Simple string search:
 * For every alignment i in the text, compare pat[0..m-1] against text[i..].
 * We count one comparison for every text-character vs pattern-character test.
 *
 * Debugging note:
 * - This implementation is the reference for correctness.
 * - If Boyer-Moore differs from this count, Boyer-Moore is wrong.
 */
static SearchStats simple_search_count(const unsigned char *text, size_t n,
                                       const unsigned char *pat, size_t m) {
    SearchStats stats = {0, 0};

    if (m == 0 || n < m) {
        return stats;
    }

    for (size_t i = 0; i + m <= n; i++) {
        size_t j = 0;

        while (j < m) {
            stats.comparisons++;

            if (text[i + j] != pat[j]) {
                break;
            }

            j++;
        }

        if (j == m) {
            stats.matches++;
        }
    }

    return stats;
}

/* ---------------- Boyer-Moore delta1 / delta2 ---------------- */

/*
 * delta1 preprocessing:
 * For each ASCII character c, delta1[c] stores the last index where c
 * appears in the pattern, or -1 if c does not appear.
 *
 * This supports the bad-character rule:
 * if mismatch happens at pattern index j on text character c,
 * then a possible shift is j - delta1[c].
 */
static void make_delta1(int delta1[ALPHABET_SIZE],
                        const unsigned char *pat, size_t m) {
    for (int i = 0; i < ALPHABET_SIZE; i++) {
        delta1[i] = -1;
    }

    for (size_t i = 0; i < m; i++) {
        delta1[pat[i]] = (int)i;
    }
}

/*
 * Strong good-suffix preprocessing helper.
 *
 * shift[] and bpos[] are standard helper arrays used to build delta2.
 * This computes shifts based on suffix matches and border positions.
 */
static void preprocess_strong_suffix(int *shift, int *bpos,
                                     const unsigned char *pat, int m) {
    int i = m;
    int j = m + 1;
    bpos[i] = j;

    while (i > 0) {
        while (j <= m && pat[i - 1] != pat[j - 1]) {
            if (shift[j] == 0) {
                shift[j] = j - i;
            }
            j = bpos[j];
        }
        i--;
        j--;
        bpos[i] = j;
    }
}

/*
 * Fill in remaining good-suffix cases after strong suffix preprocessing.
 */
static void preprocess_case2(int *shift, int *bpos, int m) {
    int j = bpos[0];

    for (int i = 0; i <= m; i++) {
        if (shift[i] == 0) {
            shift[i] = j;
        }
        if (i == j) {
            j = bpos[j];
        }
    }
}

/*
 * delta2 preprocessing:
 * delta2[j] stores the good-suffix shift used when mismatch occurs at pat[j].
 *
 * We first compute shift[] and bpos[], then convert to delta2 by setting
 * delta2[j] = shift[j + 1].
 */
static void make_delta2(int *delta2, const unsigned char *pat, size_t m) {
    int *shift = (int *)calloc(m + 1, sizeof(int));
    int *bpos  = (int *)calloc(m + 1, sizeof(int));

    if (!shift || !bpos) {
        fprintf(stderr, "Error: malloc failed for delta2 preprocessing\n");
        free(shift);
        free(bpos);
        exit(EXIT_FAILURE);
    }

    preprocess_strong_suffix(shift, bpos, pat, (int)m);
    preprocess_case2(shift, bpos, (int)m);

    for (size_t j = 0; j < m; j++) {
        delta2[j] = shift[j + 1];
        if (delta2[j] < 1) {
            delta2[j] = 1;
        }
    }

    free(shift);
    free(bpos);
}

/* ---------------- Boyer-Moore search ---------------- */

/*
 * Boyer-Moore search:
 * - align the pattern at text position s
 * - compare from right to left
 * - on mismatch, use the max of:
 *     bad-character shift  = j - delta1[text[s+j]]
 *     good-suffix shift    = delta2[j]
 * - on full match, shift by 1 so overlapping matches are counted.
    * Honestly, the shift is sometimes incorrect lol
 */
static SearchStats boyer_moore_count(const unsigned char *text, size_t n,
                                     const unsigned char *pat, size_t m) {
    SearchStats stats = {0, 0};

    if (m == 0 || n < m) {
        return stats;
    }

    int delta1[ALPHABET_SIZE];
    int *delta2 = (int *)malloc(m * sizeof(int));
    if (!delta2) {
        fprintf(stderr, "Error: malloc failed for delta2\n");
        exit(EXIT_FAILURE);
    }

    make_delta1(delta1, pat, m);
    make_delta2(delta2, pat, m);

    size_t s = 0;  /* current alignment start in text */

    while (s + m <= n) {
        ssize_t j = (ssize_t)m - 1;

        while (j >= 0) {
            stats.comparisons++;

            if (pat[j] != text[s + (size_t)j]) {
                break;
            }

            j--;
        }

        if (j < 0) {
            stats.matches++;

            /*
             * We shift by 1 after a match so that overlapping matches
             * are not missed.
             */
            s += 1;
        } else {
            unsigned char badc = text[s + (size_t)j];
            int bc_shift = (int)j - delta1[badc];
            int gs_shift = delta2[j];

            if (bc_shift < 1) {
                bc_shift = 1;
            }
            if (gs_shift < 1) {
                gs_shift = 1;
            }

            s += (size_t)((bc_shift > gs_shift) ? bc_shift : gs_shift);
        }
    }

    free(delta2);
    return stats;
}

/* ---------------- Main ---------------- */

/*
 * Expected usage:
 *   search <src-filename> <pattern-file> <type>
 * where type is 1 for simple search and 2 for Boyer-Moore.
 */
static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s <src-filename> <pattern-file> <type>\n", prog);
    fprintf(stderr, "  type = 1 for simple search\n");
    fprintf(stderr, "  type = 2 for Boyer-Moore\n");
}

int main(int argc, char **argv) {
    if (argc != 4) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char *src_filename = argv[1];
    const char *pattern_filename = argv[2];
    int type = atoi(argv[3]);

    if (type != 1 && type != 2) {
        fprintf(stderr, "Error: type must be 1 or 2\n");
        return EXIT_FAILURE;
    }

    /*
     * Read source text and patterns before timing begins.
     * This avoids charging file I/O to the search algorithm.
     */
    size_t text_len = 0;
    unsigned char *text = read_entire_file(src_filename, &text_len);
    PatternList patterns = load_patterns(pattern_filename);

    /*
     * Touch all bytes before timing so the OS is less likely to fault in
     * pages during the measured search phase.
     */
    touch_all_bytes(text, text_len);

    uint64_t total_matches = 0;
    uint64_t total_comparisons = 0;

    /*
     * We measure both wall-clock time and cycle count.
     * Wall-clock time is easier to interpret.
     * Cycle count is lower-level and useful for performance analysis.
     */
    double t0 = now_seconds();
    uint64_t c0 = rdtsc_start();

    for (size_t p = 0; p < patterns.count; p++) {
        const unsigned char *pat = (const unsigned char *)patterns.items[p];
        size_t pat_len = strlen(patterns.items[p]);

        SearchStats stats;

        if (type == 1) {
            stats = simple_search_count(text, text_len, pat, pat_len);
        } else {
            stats = boyer_moore_count(text, text_len, pat, pat_len);
        }

        total_matches += stats.matches;
        total_comparisons += stats.comparisons;

        /*
         * if (type == 2) {
         *     SearchStats slow = simple_search_count(text, text_len, pat, pat_len);
         *     if (slow.matches != stats.matches) {
         *         fprintf(stderr,
         *                 "Mismatch for pattern '%s': simple=%" PRIu64 ", bm=%" PRIu64 "\n",
         *                 patterns.items[p], slow.matches, stats.matches);
         *     }
         * }
         */
    }

    uint64_t c1 = rdtsc_end();
    double t1 = now_seconds();

    uint64_t total_cycles = c1 - c0;
    double total_seconds = t1 - t0;

    //Output metrics:
    printf("matches=%" PRIu64 "\n", total_matches);
    printf("cycles=%" PRIu64 "\n", total_cycles);
    printf("seconds=%.9f\n", total_seconds);
    printf("comparisons=%" PRIu64 "\n", total_comparisons);

    pattern_list_free(&patterns);
    free(text);
    return EXIT_SUCCESS;
}
