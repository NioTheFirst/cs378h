//  mem-copy.c                     Warren A. Hunt, Jr.

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <cstdint>
#include <cinttypes>

#define  VALUE_4096       (4096)

#define  LOG_MATRIX_SIZE  ( 15 )
#define  MAX_MATRIX_INDEX ( 1 << LOG_MATRIX_SIZE )
#define  MEMSIZE_QWORDS   ( MAX_MATRIX_INDEX * MAX_MATRIX_INDEX )
#define  MEMSIZE_BYTES    ( 8ULL * MEMSIZE_QWORDS )

#define  REPEAT_COUNT     ( 1 )

typedef long long int qword;

// Macro for index calculation
//#define  ij( i, j, n )   ( (i * n) + j )
#define ij(i,j,n) ((qword)(i) * (qword)(n) + (qword)(j))

static inline uint64_t rdtsc_start(void) {
  unsigned hi, lo;
  __asm__ __volatile__(
    "lfence\n\t"        // serialize prior loads
    "rdtsc\n\t"         // read TSC -> edx:eax
    : "=a"(lo), "=d"(hi)
    :
    : "memory"
  );
  return ((uint64_t)hi << 32) | lo;
}

static inline uint64_t rdtsc_stop(void) {
  unsigned hi, lo;
  __asm__ __volatile__(
    "rdtscp\n\t"        // read TSC + serialize later insns
    : "=a"(lo), "=d"(hi)
    :
    : "%rcx", "memory"
  );
  __asm__ __volatile__("lfence\n\t" ::: "memory");
  return ((uint64_t)hi << 32) | lo;
}

long long unsigned int_pow( qword i, qword e ) {
  if ( i < 0 ) return 1;
  qword ans = 1;
  while ( e ) { ans = ans * i; e--; };
  return( ans );
}


void init_square_matrix( qword qw_mem[], qword n ) {

  for( qword i = 0; i < n; i++ )
    for( qword j = 0; j < n; j++ )
      qw_mem[ ij( i, j, n ) ] = ij( i, j, n );
}


void print_square_matrix( qword qw_mem[], qword n ) {

  for( qword i = 0; i < n; i++ )
    for( qword j = 0; j < n; j++ )
      printf( "qw_mem %3llu is: %3lld.\n",
              ij( i, j, n ),  qw_mem[ ij( i, j, n ) ] );
  }

void transpose( qword qw_mem [], qword n, int part ) {

  switch( part ) {
  case 0:
  // typical transpose code

    for( qword r = 0; r < REPEAT_COUNT; r++ )
      for( qword i = 0; i < n; i++ )
        for( qword j = i+1; j < n; j++ ) {

          qword ij = ij( i, j, n );
          qword ji = ij( j, i, n );

          qword tmp = qw_mem[ ij ];
          qw_mem[ ij ] = qw_mem[ ji ];
          qw_mem[ ji ] = tmp;
        }
    break;

  case 1:
  // loop unrolled transpose code

    // Solution for Part 1 goes here.
    /*printf( "Part 1 is not yet implemented.\n" );
    exit( EXIT_FAILURE );
    break;*/
    // loop-unrolled transpose code (unroll inner loop by 4)
    for (qword r = 0; r < REPEAT_COUNT; r++) {
      for (qword i = 0; i < n; i++) {

        qword j = i + 1;

        // Unroll by 4
        for (; j + 3 < n; j += 4) {
          qword idx0 = ij(i, j + 0, n), jdx0 = ij(j + 0, i, n);
          qword idx1 = ij(i, j + 1, n), jdx1 = ij(j + 1, i, n);
          qword idx2 = ij(i, j + 2, n), jdx2 = ij(j + 2, i, n);
          qword idx3 = ij(i, j + 3, n), jdx3 = ij(j + 3, i, n);

          qword tmp;
          tmp = qw_mem[idx0]; qw_mem[idx0] = qw_mem[jdx0]; qw_mem[jdx0] = tmp;
          tmp = qw_mem[idx1]; qw_mem[idx1] = qw_mem[jdx1]; qw_mem[jdx1] = tmp;
          tmp = qw_mem[idx2]; qw_mem[idx2] = qw_mem[jdx2]; qw_mem[jdx2] = tmp;
          tmp = qw_mem[idx3]; qw_mem[idx3] = qw_mem[jdx3]; qw_mem[jdx3] = tmp;
        }

        // Remainder
        for (; j < n; j++) {
          qword idx = ij(i, j, n);
          qword jdx = ij(j, i, n);
          qword tmp = qw_mem[idx];
          qw_mem[idx] = qw_mem[jdx];
          qw_mem[jdx] = tmp;
        }
      }
    }
    break;


  case 2:{
  // block-by-block transpose code

    // Solution for Part 2 goes here.
    /*printf( "Part 2 is not yet implemented.\n" );
    exit( EXIT_FAILURE );
    break;*/
    const qword B = 16;

    for (qword r = 0; r < REPEAT_COUNT; r++) {

      for (qword bi = 0; bi < n; bi += B) {
        qword i_end = bi + B;
        if (i_end > n) i_end = n;

        // 1) Diagonal tile: transpose within tile (upper triangle only).
        //these ones are on the diagonal
        for (qword i = bi; i < i_end; i++) {
          for (qword j = i + 1; j < i_end; j++) {
            qword idx = ij(i, j, n);
            qword jdx = ij(j, i, n);
            qword tmp = qw_mem[idx];
            qw_mem[idx] = qw_mem[jdx];
            qw_mem[jdx] = tmp;
          }
        }

        // 2) Off-diagonal tiles: swap tile (bi,bj) with tile (bj,bi)
        //these ones are off the diagonal, we can swap the whole tile at once         
        for (qword bj = bi + B; bj < n; bj += B) {
          qword j_end = bj + B;
          if (j_end > n) j_end = n;

          for (qword i = bi; i < i_end; i++) {
            for (qword j = bj; j < j_end; j++) {
              qword idx = ij(i, j, n);
              qword jdx = ij(j, i, n);
              qword tmp = qw_mem[idx];
              qw_mem[idx] = qw_mem[jdx];
              qw_mem[jdx] = tmp;
            }
          }
        }
      }
    }
    break;
  }


  default:
    printf( "Unexpected part.\n" );
    exit( EXIT_FAILURE );
    break;
    }
}

int check_transpose( qword qw_mem [], qword n ) {

  int matrix_ok = 1;

  for( qword i = 0; i < n; i++ )
    for( qword j = i+1; j < n; j++ )
      matrix_ok &= qw_mem[ ij( i, j, n ) ] == ij( j, i, n );

  return( matrix_ok );
}


int main ( int argc, char* argv[], char* env[] ) {

  unsigned long long from, to, cnt, matrix_size;
  int part = 0;
  int matrix_ok = 1;
  char *endptr;

  if (argc != 3) {
     fprintf(stderr, "Usage: %s <log_size> <case_number>\n", argv[0] );
     return EXIT_FAILURE;
    }

  qword log_matrix_size = atoi( argv[1] );
  fprintf(stderr, "log_matrix_size = %lld.\n", log_matrix_size );

  int at_least_1   = 1 <= log_matrix_size;
  int le_than_15   = log_matrix_size <= LOG_MATRIX_SIZE;
  int in_range     = at_least_1 && le_than_15;
  int not_in_range = !( in_range );

  if ( not_in_range )   {
    fprintf(stderr, "Log of matrix dimension %lld is out of range of 1..15.\n",
            log_matrix_size );
    exit( EXIT_FAILURE );
  }

  int case_num = atoi( argv[2] );
  fprintf(stderr, "Part = %d.\n", case_num );
  if ( !( case_num == 0 || case_num == 1 ||  case_num == 2 ) ) {
        fprintf(stderr, "Illegal Part Number: %d!\n",
            case_num );
    exit( EXIT_FAILURE );
  }

  // Allocate memory
  //void *mem = aligned_alloc( VALUE_4096, (qword) MEMSIZE_BYTES );
  //qword* mem = (qword*) aligned_alloc(VALUE_4096, (size_t)MEMSIZE_BYTES);

  /*if (mem == NULL) {
    perror("Failed to allocate aligned memory.");
    exit(  EXIT_FAILURE) ;
  }

  printf( "Allocated %llu 4K-aligned bytes at address %p.\n",
          (qword) MEMSIZE_BYTES,
          mem);
  */

  /*for( qword r = 0; r <= log_matrix_size; r++ ) {
    matrix_ok = 1;
    matrix_size = int_pow( 2, r );

    init_square_matrix( mem, matrix_size );

    // Start individual TRANSPOSE timing here!

    uint64_t t0 = rdtsc_start();

    transpose( mem, matrix_size, case_num );

    // End TRANSPOSE time here!

    uint64_t t1 = rdtsc_stop();
    uint64_t cycles = t1 - t0;

    printf("n=%5llu  cycles=%" PRIu64 "  cycles/elem=%0.2f\n",
        (unsigned long long)matrix_size,
        cycles,
        (double)cycles / ((double)matrix_size * (double)matrix_size));

    // Check that transpose returned an expected answer.
    matrix_ok = check_transpose( mem, matrix_size );

    printf( "Matrix transpose of size %5lld is (Bad 0, OK 1):  %d.\n", matrix_size, matrix_ok );
  }*/

  // primes for "+/- first 10 primes"
  static const int primes[10] = {2,3,5,7,11,13,17,19,23,29};

  int min_pow = 7;
  int max_pow = 15;

  // compute maximum N we will test to size the allocation
  qword max_n = (1LL << max_pow) + primes[9];

  // allocate for max_n x max_n
  size_t mem_qwords = (size_t)max_n * (size_t)max_n;
  size_t mem_bytes  = mem_qwords * sizeof(qword);

  qword* mem = (qword*) aligned_alloc(VALUE_4096, (mem_bytes + VALUE_4096 - 1) / VALUE_4096 * VALUE_4096);
  if (!mem) { perror("aligned_alloc"); exit(EXIT_FAILURE); }

  // print CSV header
  printf("part,core_n,test_n,delta,cycles,cycles_per_elem,ok\n");

  for (int p = min_pow; p <= max_pow; p++) {
    qword core = 1LL << p;

    // run core itself (delta=0)
    {
      qword n = core;
      init_square_matrix(mem, n);
      uint64_t t0 = rdtsc_start();
      transpose(mem, n, case_num);
      uint64_t t1 = rdtsc_stop();
      uint64_t cycles = t1 - t0;
      int ok = check_transpose(mem, n);
      printf("%d,%lld,%lld,%d,%" PRIu64 ",%.6f,%d\n",
            case_num, (long long)core, (long long)n, 0, cycles,
            (double)cycles / ((double)n * (double)n), ok);
    }

    // run +/- primes
    for (int i = 0; i < 10; i++) {
      int d = primes[i];

      // core - d
      {
        qword n = core - d;
        init_square_matrix(mem, n);
        uint64_t t0 = rdtsc_start();
        transpose(mem, n, case_num);
        uint64_t t1 = rdtsc_stop();
        uint64_t cycles = t1 - t0;
        int ok = check_transpose(mem, n);
        printf("%d,%lld,%lld,%d,%" PRIu64 ",%.6f,%d\n",
              case_num, (long long)core, (long long)n, -d, cycles,
              (double)cycles / ((double)n * (double)n), ok);
      }

      // core + d
      {
        qword n = core + d;
        init_square_matrix(mem, n);
        uint64_t t0 = rdtsc_start();
        transpose(mem, n, case_num);
        uint64_t t1 = rdtsc_stop();
        uint64_t cycles = t1 - t0;
        int ok = check_transpose(mem, n);
        printf("%d,%lld,%lld,%d,%" PRIu64 ",%.6f,%d\n",
              case_num, (long long)core, (long long)n, +d, cycles,
              (double)cycles / ((double)n * (double)n), ok);
      }
    }
  }

  free( mem );  // Should we call ``aligned_free''?  Not available on MacOS.

  printf( "Exit code: %d.\n", EXIT_SUCCESS );

  exit( EXIT_SUCCESS );
}

