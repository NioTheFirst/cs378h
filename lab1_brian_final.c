
//  mem-copy.c                     Warren A. Hunt, Jr.
/*
Correctness (overlap semantics).
  Part 0: byte copy forward; only valid for non-overlapping src/dst (checked by harness).
  Part 1: memmove semantics for any overlap:
    - If dst < src OR regions do not overlap: copy forward byte-by-byte.
    - Else (dst > src and overlap): copy backward byte-by-byte from the end.
    This avoids overwriting source bytes before they are read.
  Part 2: same direction rule as Part 1, but uses 4-byte word copies when safe:
    - Strip bytes until BOTH src and dst are 4-byte aligned (addr % 4 == 0), then copy
      as uint32_t words, then copy any remaining tail bytes.
    - For backward copy, strip from the end until end pointers are aligned, then copy
      words backward (*--wd = *--ws), then finish remaining bytes backward.
  Part 3: uses x86 string instruction REP MOVSB for speed:
    - Forward copy: CLD; REP MOVSB
    - Backward-overlap: set pointers to end-1; STD; REP MOVSB; CLD
    - No longer does word copies, instead relies on hardware acceleration.
  Need CLD diff for backwards copy

The main intuition behind the copy strategies to make it "in-place" is that the src and dst are the same size. Hence, any overlap means that the dst region starts inside the src region or vice versa. 
By checking the relative positions of src and dst, we can determine the safe direction to copy without overwriting data that has not yet been copied.
This avoids the case when you copy byte A into A', but find that A' is needed later in the copy process.

Measurement methodology.
  We time the copy call (mem_copy_bytes -> mem_copy_parts) using:
    - cycles: serialized rdtsc (LFENCE; RDTSC; LFENCE) before/after
    - time: clock_gettime(CLOCK_MONOTONIC_RAW) before/after
  To reduce noise for small sizes (220/228 bytes), we repeat the copy for 100 iterations and
  compute: total_bytes = cnt * ITERS, and GiB/s_time  = total_bytes / seconds / 2^30 cycles/byte = (stop_tsc - start_tsc) / total_bytes

# -------- Non-overlap baseline (forward-safe) --------

# Part 0: byte copy (non-overlap required)
./lab1_brian 32 2000000   1048576    0   # 2^20  cycles/byte=0.3822  GiB/s=6.551
./lab1_brian 32 300000000 268435456  0   # 2^28  cycles/byte=0.4013  GiB/s=6.239

# Part 1: byte copy + overlap logic
./lab1_brian 32 2000000   1048576    1   cycles/byte=0.3771  GiB/s=6.638
./lab1_brian 32 300000000 268435456  1   cycles/byte=0.4001  GiB/s=6.257

# Part 2: word copy (aligned)
./lab1_brian 32 2000000   1048576    2   cycles/byte=0.1269  GiB/s=19.730
./lab1_brian 32 300000000 268435456  2   cycles/byte=0.2614  GiB/s=9.576

# Part 3: REP MOVSB (forward, ERMS)
./lab1_brian 32 2000000   1048576    3   cycles/byte=0.0527  GiB/s=47.459
./lab1_brian 32 300000000 268435456  3    cycles/byte=0.2202  GiB/s=11.368

# -------- Overlap dst>src: tiny overlap (dst = src + 8) --------
./lab1_brian 32 40  1048576     1 cycles/byte=0.5300  GiB/s=4.724
./lab1_brian 32 40  268435456   1  cycles/byte=0.4755  GiB/s=5.264
./lab1_brian 32 40  1048576     2 cycles/byte=0.1153  GiB/s=21.717
./lab1_brian 32 40  268435456   2 cycles/byte=0.2570  GiB/s=9.742
./lab1_brian 32 40  1048576     3 cycles/byte=0.5243  GiB/s=4.775
./lab1_brian 32 40  268435456   3  cycles/byte=0.5447  GiB/s=4.596

# -------- Overlap dst>src: barely overlapping at the tail --------
# For 2^20, from+cnt = 32 + 1048576 = 1048608
# Choose to = 1048500 (overlap = 108 bytes)
./lab1_brian 32 1048500   1048576     3  cycles/byte=0.5226  GiB/s=4.790

# For 2^28, from+cnt = 32 + 268435456 = 268435488
# Choose to = 268435200 (overlap = 288 bytes)
./lab1_brian 32 268435200 268435456   3  cycles/byte=0.5336  GiB/s=4.691

# -------- Misalignment (exercise Part 2 peeling) --------
./lab1_brian 33 49  1048576     2   cycles/byte=0.1110  GiB/s=22.558
./lab1_brian 33 49  268435456   2    cycles/byte=0.2197  GiB/s=11.396

Observed trends:
- [P1 vs P0] We see that the timing for part 1 basically matches part 0. This is expected, as the only additional overhead is the overlap checks. The copy itself is the same byte-by-byte copy.
- [Front vs back] When we compare back wards copying (in the overlap section), we can see that it is slower (33%) than forward copying. One possible explanation is that the CPU may not be optimized for backward copying?
- [Word copy] We observe that word copy is significantly faster than byte copy for non-overlapping cases. Also, while not that much, it stillhas wins for overlapping cases. This is expected, as we are copying 4 bytes at a time instead of 1 byte at a time, which reduces the number of iterations and overhead.
- [Misalignment] We see that misalignment does have a performance impact, but it is not very significant. The peeling process adds some overhead, but the overall performance is still good for the vast amount of data we are copying.
- [REP MOVSB] The REP MOVSB instruction shows the best performance for non-overlapping copies, thanks to hardware acceleration. However, for overlapping copies, especially when dst > src, the performance drops significantly. It seems that hardware acceleration is not utilized in backwards copies. This behaviour has been recorded in over 1000 iterations (including small and large data copying).
*/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <inttypes.h>
#include <x86intrin.h>   // for __rdtsc()

#define  VALUE_1024      (1024)
#define  MEMSIZE_BYTES   (VALUE_1024 * VALUE_1024 * VALUE_1024)
#define  MEMSIZE_WORDS   (MEMSIZE_BYTES / 4)

#define SRC_OFFSET       (32)
#define DST_OFFSET       (48)

// Array type declaration for a union type of bytes and 4-byte words.
// If you prefer arrays, but you should get used to working with pointers...
union memory {
  char byte_mem [ MEMSIZE_BYTES ] ;
  int  word_mem [ MEMSIZE_WORDS ] ;
};

static inline uint64_t rdtsc_start(void) {
  _mm_lfence();
  uint64_t t = __rdtsc();
  _mm_lfence();
  return t;
}

static inline uint64_t rdtsc_stop(void) {
  _mm_lfence();
  uint64_t t = __rdtsc();
  _mm_lfence();
  return t;
}

static inline int ranges_overlap(const char* src, const char* dst, long long cnt) {
  // [src, src+cnt) overlaps [dst, dst+cnt)?
  return (src < dst + cnt) && (dst < src + cnt);
}


int mem_copy_parts( char* src, char* dst, long long int cnt, int part) {

  char * src_end = src + cnt;

  switch( part ) {
  case 0:
    // Non-operlapping copy
    while ( src < src_end ) *dst++ = *src++;
    return( 0 );
    break;

  case 1:{
    // memmove-style byte copy (handles overlap)
    if (!ranges_overlap(src, dst, cnt) || dst < src) {
      // forward
      while (src < src_end) *dst++ = *src++; //this is a one line copy of bytes from src to dst
    } else {
      // backward
      char* s = src + cnt;
      char* d = dst + cnt;
      while (s > src) *--d = *--s; //the same copy as above but backwards
    }
    return 0;
  }

  case 2:{
    // choose direction (forward/backward) same as Part 1
    // strip bytes until both pointers are 4-byte aligned (or cnt too small)
    //  bulk copy uint32_t words
    //   copy remaining tail bytes
    //

    if (!ranges_overlap(src, dst, cnt) || dst < src) {
      // -------- forward --------
      // 1) strip until both aligned or cnt < 4
      while (cnt > 0 &&
              (((uintptr_t)src & 3u) != 0u || ((uintptr_t)dst & 3u) != 0u)) {  //this arithmetic zeroes out the last 2 bits to check for 4-byte alignment
        *dst++ = *src++;
        cnt--;
      }

      // 2) word copy
      uint32_t* ws = (uint32_t*)src;
      uint32_t* wd = (uint32_t*)dst;
      while (cnt >= 4) {
        *wd++ = *ws++;
        cnt -= 4;
      }

      // 3) tail bytes
      src = (char*)ws;
      dst = (char*)wd;
      while (cnt-- > 0) *dst++ = *src++;

    } else {
      // -------- backward -------- (copied from above, but changed order)
      char* s = src + cnt;
      char* d = dst + cnt;

      // 1) strip tail bytes until both aligned (at end) or cnt < 4
      while (cnt > 0 &&
              (((uintptr_t)s & 3u) != 0u || ((uintptr_t)d & 3u) != 0u)) {
        *--d = *--s;
        cnt--;
      }

      // 2) word copy backward
      uint32_t* ws = (uint32_t*)s;
      uint32_t* wd = (uint32_t*)d;
      while (cnt >= 4) {
        *--wd = *--ws;
        cnt -= 4;
      }

      // 3) remaining bytes backward
      s = (char*)ws;
      d = (char*)wd;
      while (cnt-- > 0) *--d = *--s;
    }
    return 0;
  }

  case 3:{
      // Go-all-out: use rep movsb in assembly.
      // BUT: rep movsb copies forward by default (DF=0 via cld).
      // For overlap where dst > src, you must copy backward:
      //   set DF=1 (std), set RSI/RDI to end-1, use rep movsb, then clear DF (cld).

      if (!ranges_overlap(src, dst, cnt) || dst < src) {
        // forward rep movsb
        __asm__ __volatile__(
          "cld\n\t"               // DF=0
          "rep movsb\n\t"
          : "+S"(src), "+D"(dst), "+c"(cnt)
          :
          : "memory"
        );
      } else {
        // backward rep movsb
        char* s = src + cnt - 1;
        char* d = dst + cnt - 1;
        __asm__ __volatile__(
          "std\n\t"               // DF=1 (decrement)
          "rep movsb\n\t"
          "cld\n\t"               // restore DF=0 (important for rest of program, which may or may not depend on DF=0)
          : "+S"(s), "+D"(d), "+c"(cnt)
          :
          : "memory"
        );
      }
      return 0;
    }

  default:
    printf( "Bad Part number: %d.\n", part );
    exit( 1 );
    }
  }

int mem_copy_bytes
( char *arr,         // Array start location
  char *arr_end,     // Array end location
  char *src,         // Start of content to copy
  char *dst,         // Destination for content
  long long int cnt, // Number of bytes
  int part
  ) {

  // Check for safe accesses

  char * src_end = src + cnt;
  char * dst_end = dst + cnt;

  if ( src < arr )          // Source region starts outside of array
    return 1;

  if ( src_end >= arr_end ) // Source region continues outside of array
    return 2;

  if ( dst < arr )          // Destination region starts outside of array
    return 3;

  if ( dst_end >= arr_end ) // Destination region continues outside of array
    return 4;

  // Source and Destination regions overlap
  if (  ( part == 0 ) &&      // Only check for src-dest overlap for Part 0.
        ( src <= dst     && dst     < src_end ) ||  // dst "inside" source region
        ( src <= dst_end && dst_end < src_end ) )   // dst_end "inside" source
    return 5;

  // Perform the memory copy.
  mem_copy_parts( src, dst, cnt, part );

  return 0;
}


int main ( int argc, char* argv[], char* env[] ) {

  unsigned long long from, to, cnt;
  int part = 0;
  char *endptr;

  if (argc < 5) {
     fprintf(stderr, "Usage: %s <from> <to> <cnt> <part>\n", argv[0]);
     return EXIT_FAILURE;
    }

  // Convert string input arguments to numbers.

  // This read-number and check-result code provide by Google AI Overview
  errno = 0;  //Clear errno before call
  from = strtoll(argv[1], &endptr, 10);

    // Check for errors
    if (errno == ERANGE) {
        fprintf(stderr, "Error: The number provided is out of the range for a long long.\n");
        return EXIT_FAILURE;
    } else if (endptr == argv[1]) {
        fprintf(stderr, "Error: No digits were found in the argument.\n");
        return EXIT_FAILURE;
    } else if (*endptr != '\0') {
        fprintf(stderr, "Error: Further characters found after the number: %s\n", endptr);
        // Handle partial conversion if necessary
    }

    // If conversion is successful, use the value
    printf("Successfully read long long value: %lld\n", from); // %lld is the format specifier
    // return EXIT_SUCCESS;

  // Convert string input arguments to numbers:
  errno = 0;  //Clear errno before call
  to = strtoll(argv[2], &endptr, 10);

    // Check for errors
    if (errno == ERANGE) {
        fprintf(stderr, "Error: The number provided is out of the range for a long long.\n");
        return EXIT_FAILURE;
    } else if (endptr == argv[1]) {
        fprintf(stderr, "Error: No digits were found in the argument.\n");
        return EXIT_FAILURE;
    } else if (*endptr != '\0') {
        fprintf(stderr, "Error: Further characters found after the number: %s\n", endptr);
        // Handle partial conversion if necessary
    }

    // If conversion is successful, use the value
    printf("Successfully read long long value: %lld\n", to); // %lld is the format specifier
    // return EXIT_SUCCESS;

  // Convert string input arguments to numbers:
  errno = 0;  //Clear errno before call
  cnt = strtoll(argv[3], &endptr, 10);

    // Check for errors
    if (errno == ERANGE) {
        fprintf(stderr, "Error: The number provided is out of the range for a long long.\n");
        return EXIT_FAILURE;
    } else if (endptr == argv[1]) {
        fprintf(stderr, "Error: No digits were found in the argument.\n");
        return EXIT_FAILURE;
    } else if (*endptr != '\0') {
        fprintf(stderr, "Error: Further characters found after the number: %s\n", endptr);
        // Handle partial conversion if necessary
    }

    // If conversion is successful, use the value
    printf("Successfully read long long value: %lld\n", cnt); // %lld is the format specifier
    // return EXIT_SUCCESS;

  // Convert string input arguments to numbers:
  errno = 0;  //Clear errno before call
  part = strtoll(argv[4], &endptr, 10);

    // Check for errors
    if (errno == ERANGE) {
        fprintf(stderr, "Error: The number provided is out of the range for a long long.\n");
        return EXIT_FAILURE;
    } else if (endptr == argv[1]) {
        fprintf(stderr, "Error: No digits were found in the argument.\n");
        return EXIT_FAILURE;
    } else if (*endptr != '\0') {
        fprintf(stderr, "Error: Further characters found after the number: %s\n", endptr);
        // Handle partial conversion if necessary
    }

    // If conversion is successful, use the value
    printf("Successfully read long long value: %d\n", part); // %lld is the format specifier
    // return EXIT_SUCCESS;


  // Allocate large array
  union memory *ram_memory = malloc( MEMSIZE_BYTES );
  if ( ram_memory == NULL ) {
    printf( "Array allocation failed!\n" );
    exit( 1 );
  }

  // We will work with pointers from here on...

  char *mem = (char *) ram_memory;
  char *mem_end = mem + MEMSIZE_BYTES;
  int result_code = 0;

  // Initialize the "memory".
  for ( long long int i = 0;  i < MEMSIZE_BYTES;  i++ ) *(mem + i) = (char) i;

  printf( "\nAllocated %llu memory bytes.\n\n", (unsigned long long) MEMSIZE_BYTES );

  // Debugging: print some bytes
  for ( long long int j = 32;  j < 64;  j++ )
    printf( "Index:  %llu, Value: %c\n", j, *(mem + j) );

  // Start timing here!

  // Call memory-copy subroutine
  /*result_code = mem_copy_bytes
    (
     mem,            // Array start address
     mem_end,        // Array end address
     mem + from,     // Copy source address
     mem + to,       // Copy destination address
     cnt,            // Number of bytes to copy
     part            // Lab Part number
     );*/

  // -------------------- Start timing here! --------------------
  const int ITERS = 100;   // tune; 1e6 is good for 220/228B
  struct timespec t0, t1;

  clock_gettime(CLOCK_MONOTONIC_RAW, &t0);
  uint64_t c0 = rdtsc_start();

  for (int it = 0; it < ITERS; it++) {
    result_code = mem_copy_bytes(
      mem,            // Array start address
      mem_end,        // Array end address
      mem + from,     // Copy source address
      mem + to,       // Copy destination address
      (long long)cnt, // Number of bytes to copy
      part            // Lab Part number
    );
  }

  uint64_t c1 = rdtsc_stop();
  clock_gettime(CLOCK_MONOTONIC_RAW, &t1);
  // -------------------- End timing here! --------------------

  // Compute stats
  double sec = (t1.tv_sec - t0.tv_sec) + 1e-9 * (double)(t1.tv_nsec - t0.tv_nsec);
  double total_bytes = (double)cnt * (double)ITERS;
  double gibps = (total_bytes / sec) / (1024.0 * 1024.0 * 1024.0);
  double cycles_per_byte = (double)(c1 - c0) / total_bytes;

  printf("RESULT code=%d  iters=%d  bytes/iter=%llu  sec=%.6f  cycles=%" PRIu64
        "  cycles/byte=%.4f  GiB/s=%.3f\n",
        result_code, ITERS, cnt, sec, (uint64_t)(c1 - c0), cycles_per_byte, gibps);

  // End timing here!

  printf( "\nResult code from copy: %d,  Bytes copied %lld.\n\n",
          result_code, cnt );

  // Debugging: print some bytes
  for ( long long int j = 32;  j < 64;  j++ )
    printf( "Index:  %llu, Value: %c\n", j, *(mem + j) );

  //  Can we call a subroutine to confirm the correctness of the answer?

  exit( 0 );
}

