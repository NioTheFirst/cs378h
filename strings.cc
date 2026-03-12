/*
Name: Brian Zhang
UTID: bz5346

CS378h Lab: String Search Performance

This program implements:
1) a simple one-by-one string search
2) a Boyer-Moore string search using explicit delta1 and delta2 tables

Invocation:
    pat_search <src-filename> <pattern-file> <type>

Arguments:
    <src-filename>  : source text file
    <pattern-file>  : file containing one pattern per line
    <type>          : 1 = simple search, 2 = Boyer-Moore

Behavior:
- Reads the full source file before timing.
- Touches all bytes before timing to reduce paging effects.
- Searches every pattern in the pattern file against the source text.
- Counts overlapping matches.
- Includes Boyer-Moore preprocessing time in the timed interval.
- Uses rdtsc cycle counts instead of wall-clock timing.
- Derives comparison metrics for both algorithms.

Compilation:
    gcc -O3 -std=c11 -Wall -Wextra -pedantic -o pat_search bz5346-search.c

Example usage:
    ./pat_search source.txt patterns.txt 1
    ./pat_search source.txt patterns.txt 2
*/

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/types.h>
#include <x86intrin.h>

#define ALPHABET_SIZE 256

typedef struct {
    uint64_t matches;
    uint64_t comparisons;
} SearchStats;

/* ---------------- rdtsc timing ---------------- */

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

/* ---------------- File reading ---------------- */

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

static volatile unsigned long long sink_sum = 0;

static void touch_all_bytes(const unsigned char *buf, size_t len) {
    unsigned long long sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += buf[i];
    }
    sink_sum = sum;
}

/* ---------------- Pattern loading ---------------- */

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

static void strip_newline_and_cr(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[n - 1] = '\0';
        n--;
    }
}

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
 * delta1[c] = last index in pat where c appears, or -1 if absent.
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
 * Standard strong good-suffix preprocessing.
 * Builds shift[0..m] and bpos[0..m], then delta2[j] = shift[j+1].
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

    size_t s = 0;  /* alignment start in text */

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
             * Shift by 1 after a full match so overlapping matches are counted.
             * Conservative but correct for substring counting.
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

    size_t text_len = 0;
    unsigned char *text = read_entire_file(src_filename, &text_len);
    PatternList patterns = load_patterns(pattern_filename);

    touch_all_bytes(text, text_len);

    uint64_t total_matches = 0;
    uint64_t total_comparisons = 0;

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
    }

    uint64_t c1 = rdtsc_end();
    uint64_t total_cycles = c1 - c0;

    double cycles_per_comparison = 0.0;
    double comparisons_per_byte = 0.0;
    double cycles_per_byte = 0.0;

    if (total_comparisons != 0) {
        cycles_per_comparison = (double)total_cycles / (double)total_comparisons;
    }
    if (text_len != 0) {
        comparisons_per_byte = (double)total_comparisons / (double)text_len;
        cycles_per_byte = (double)total_cycles / (double)text_len;
    }

    printf("matches=%" PRIu64 "\n", total_matches);
    printf("cycles=%" PRIu64 "\n", total_cycles);
    printf("comparisons=%" PRIu64 "\n", total_comparisons);
    printf("cycles_per_comparison=%.6f\n", cycles_per_comparison);
    printf("comparisons_per_byte=%.6f\n", comparisons_per_byte);
    printf("cycles_per_byte=%.6f\n", cycles_per_byte);

    pattern_list_free(&patterns);
    free(text);
    return EXIT_SUCCESS;
}
