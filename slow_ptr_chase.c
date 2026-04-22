
/*

Name: Brian Zhang

UTEID: bz5346

Fundamentally, the idea is to slow down the pointer chase through randomized permutation of pointer traversal indices.

It is quite similar to the first or second HW/lab, but the idea is that the slow traversal is slow as:
1) permutations break spatial locality and stride hence prefetchers are less effective
2) access patterns cause accesses to jump across memory pages, which heavily impacts DRAM and memory fetchs
3) Each load is highly depdent on the last one, thwarting prefetching multiple items at once (in fact the heuristic where I may sometimes bring in adjacent things becomes detrimental and clutters the cache.

In particular, the code does N things.
First, it determines the number of pages, then shuffles them. This helps break DRAM preloading (and also breaks expectations from prefetchers).
Second, it determines how many items can fit in a page, then shuffles the offsets. This helps break stride access as well as multiple degree prefetching (which fetch things and things around it).
Third, it reshuffles things around so that different page/offsets are the locations for consecutive offsets. This means that with very high likelyhood, I need to fetch a page, and then that page is immediately replaced on the next item.
After that, we shuffle randomly again (for fun), and this concludes the initialization.

To run, simply compile in c style, i.e.

gcc -O2 slow_ptr_chase.cc -o slow_ptr

Then run with ./slow_ptr

Our metrics are the same ones defined here, which is essentiall elapsed CPU time. 
The effects are already visible here, so I did not add rdtsc timing.

Here is an example of the output. The results were collected in my local server, but I expect the same leve of results on the onyx server as well.

Sample Results:
The variable array_size is:  4096.
Fast: result =     4321,  elasped_time =        0,  elapsed  0.000143.
Slow: result =     6369,  elasped_time =        0,  elapsed  0.000143.
The variable array_size is:  8192.
Fast: result =     4321,  elasped_time =        0,  elapsed  0.000290.
Slow: result =     3225,  elasped_time =        0,  elapsed  0.000459.
The variable array_size is:  16384.
Fast: result =     4321,  elasped_time =        0,  elapsed  0.000584.
Slow: result =    12939,  elasped_time =        0,  elapsed  0.000935.
The variable array_size is:  32768.
Fast: result =     4321,  elasped_time =        0,  elapsed  0.001125.
Slow: result =    17588,  elasped_time =        0,  elapsed  0.003751.
The variable array_size is:  65536.
Fast: result =     4321,  elasped_time =        0,  elapsed  0.001479.
Slow: result =     3780,  elasped_time =        0,  elapsed  0.004477.
The variable array_size is:  131072.
Fast: result =     4321,  elasped_time =        0,  elapsed  0.002212.
Slow: result =     7923,  elasped_time =        0,  elapsed  0.009273.
The variable array_size is:  262144.
Fast: result =     4321,  elasped_time =        0,  elapsed  0.004502.
Slow: result =    74819,  elasped_time =        0,  elapsed  0.039113.
The variable array_size is:  524288.
Fast: result =     4321,  elasped_time =        0,  elapsed  0.010976.
Slow: result =    52277,  elasped_time =        0,  elapsed  0.098433.
The variable array_size is:  1048576.
Fast: result =     4321,  elasped_time =        0,  elapsed  0.023944.
Slow: result =   214653,  elasped_time =        0,  elapsed  0.345948.
The variable array_size is:  2097152.
Fast: result =     4321,  elasped_time =        0,  elapsed  0.050041.
The variable array_size is:  2097152.
Fast: result =     4321,  elasped_time =        0,  elapsed  0.050041.
Slow: result =  1249905,  elasped_time =        0,  elapsed  1.354974.
The variable array_size is:  4194304.
Fast: result =     4321,  elasped_time =        0,  elapsed  0.090285.
Slow: result =   223953,  elasped_time =        0,  elapsed  5.836905.
The variable array_size is:  8388608.
Fast: result =     4321,  elasped_time =        0,  elapsed  0.160541.
Slow: result =  1941156,  elasped_time =        0,  elapsed 14.679000.
The variable array_size is:  16777216.
Fast: result =     4321,  elasped_time =        0,  elapsed  0.295563.
Slow: result = 15744962,  elasped_time =        0,  elapsed 31.679356.
The variable array_size is:  33554432.
Fast: result =     4321,  elasped_time =        0,  elapsed  0.673917.
Slow: result = 21277467,  elasped_time =        0,  elapsed 67.342325.
The variable array_size is:  67108864.
Fast: result =     4321,  elasped_time =        0,  elapsed  1.349724.
Slow: result = 52365424,  elasped_time =        0,  elapsed 146.630469.
The variable array_size is:  134217728.
Fast: result =     4321,  elasped_time =        0,  elapsed  2.611603.
Slow: result = 18998027,  elasped_time =        0,  elapsed 340.538011.
The variable array_size is:  268435456.
Fast: result =     4321,  elasped_time =        0,  elapsed  6.078894.
Slow: result = 88813441,  elasped_time =        0,  elapsed 923.073113.
The variable array_size is:  536870912.


Observe that when the variable array_sizes are small, there is negligible difference in timing. 
Or rather, you can already see small effects (i.e. by 8192, the elapsed time of the slow is twice that of fast), but in the grand scheme of things, it does not matter much for this program.
However, the slowdown is exponential, and with more pages, there is more chance to randomize, and the more deterimental the frequent page traversal is.
Observe that at the later iterations, the fast variable array for 268435356 elements takes only 6 seconds for the fast iteration, but takes a whopping 15 minutes for the slow iteration. 
This is a slowdown of 99.3%, and demonstrates that with a randomized and unpredictalbe access pattern, the CPU is essentially stalled.
It also highlights how we really don't see these impacts in major services today (or rather, rarely). 
It highlights how in general usage, the majority of access patterns are the ones which CPUs are good at, but at the tail end of the spectrum, malicious or poorly crafted code can heavily impact performance.

*/


#include "stdio.h"
#include "stdlib.h"
#include "time.h"

#define PAGE               (4096) // 2^12
#define LOG_ARRAY_SIZE     (12)   // For chase32, 5 is minimum shift amount
#define MAX_LOG_ARRAY_SIZE (30)   // Max log of array size
#define ARRAY_SIZE         (1 << (MAX_LOG_ARRAY_SIZE)) // Size of array

#define REPEAT             (16)   // Repeat experiment


void initialize_fast_chase( long int ar[], long int size ) {
  // Simple initialization
  long int i;

  for( i= 0; i < size-1; i++ )
    ar[ i ] = i+1;
  // Loop back to first array location
  ar[ size-1 ] = 0;
  }

// Simple, three, 4K-page array initialization.

//   Page 0  Page 1  Page 2
//  +-------+-------+-------+
//  |     0 |   512 |  1024 |
//  |     1 |   513 |  1025 |
//  |     2 |   514 |  1026 |
//  |   ... |   ... |   ... |
//  |   510 |  1022 |  1534 |
//  |   511 |  1023 |     0 |
//  +-------+-------+-------+

void initialize_slow_chase(long int ar[], long int size) {
    long int elems_per_page = PAGE / sizeof(long int);
    long int num_pages = size / elems_per_page;
    long int remainder = size % elems_per_page;

    long int i, j;

    // If too small to span multiple pages, just randomize the whole array.
    if (num_pages <= 1) {
        long int *perm = malloc(size * sizeof(long int));
        if (!perm) {
            perror("malloc");
            exit(1);
        }

        for (i = 0; i < size; i++) perm[i] = i;

        for (i = size - 1; i > 0; i--) {
            j = rand() % (i + 1);
            long int tmp = perm[i];
            perm[i] = perm[j];
            perm[j] = tmp;
        }

        for (i = 0; i < size - 1; i++) {
            ar[perm[i]] = perm[i + 1];
        }
        ar[perm[size - 1]] = 0;

        free(perm);
        return;
    }

    // Shuffle page order
    long int *pages = malloc(num_pages * sizeof(long int));
    if (!pages) {
        perror("malloc");
        exit(1);
    }
    for (i = 0; i < num_pages; i++) pages[i] = i;

    for (i = num_pages - 1; i > 0; i--) {
        j = rand() % (i + 1);
        long int tmp = pages[i];
        pages[i] = pages[j];
        pages[j] = tmp;
    }

    // Shuffle offsets within a page
    long int *offsets = malloc(elems_per_page * sizeof(long int));
    if (!offsets) {
        perror("malloc");
        exit(1);
    }
    for (i = 0; i < elems_per_page; i++) offsets[i] = i;

    for (i = elems_per_page - 1; i > 0; i--) {
        j = rand() % (i + 1);
        long int tmp = offsets[i];
        offsets[i] = offsets[j];
        offsets[j] = tmp;
    }

    // Build a global permutation that alternates across shuffled pages.
    // This is worse for locality than walking one page at a time.
    long int *perm = malloc(size * sizeof(long int));
    if (!perm) {
        perror("malloc");
        exit(1);
    }

    long int k = 0;

    for (i = 0; i < elems_per_page; i++) {
        for (j = 0; j < num_pages; j++) {
            long int idx = pages[j] * elems_per_page + offsets[i];
            if (idx < size) {
                perm[k++] = idx;
            }
        }
    }

    // Handle leftover elements if size is not page-aligned
    for (i = num_pages * elems_per_page; i < size; i++) {
        perm[k++] = i;
    }

    // One final shuffle over the whole permutation can make it even less regular
    for (i = k - 1; i > 0; i--) {
        j = rand() % (i + 1);
        long int tmp = perm[i];
        perm[i] = perm[j];
        perm[j] = tmp;
    }

    // Ensure traversal starts at 0, since chase32(ar, 0, ...) begins from 0.
    // Put 0 at the front of the permutation.
    long int zero_pos = -1;
    for (i = 0; i < k; i++) {
        if (perm[i] == 0) {
            zero_pos = i;
            break;
        }
    }
    if (zero_pos > 0) {
        long int tmp = perm[0];
        perm[0] = perm[zero_pos];
        perm[zero_pos] = tmp;
    }

    // Link the permutation into a single cycle ending at 0
    for (i = 0; i < k - 1; i++) {
        ar[perm[i]] = perm[i + 1];
    }
    ar[perm[k - 1]] = 0;

    free(pages);
    free(offsets);
    free(perm);
}


long int chase( long int ar[], long int start, long int sum ) {
  // Routine not used, but it might serve as a useful reference.

  long int i = start;

  do {
    i = ar[i];
    sum = sum + i;
    }
  while( i );

  return sum;
}


long int chase32( long int ar[], long int start, long int xor_sum ) {
  long int i = start;

  do {
    i = ar[i];   //  1
    i = ar[i];   //  2
    i = ar[i];   //  3
    i = ar[i];   //  4
    i = ar[i];   //  5
    i = ar[i];   //  6
    i = ar[i];   //  7
    i = ar[i];   //  8

    i = ar[i];   //  9
    i = ar[i];   // 10
    i = ar[i];   // 11
    i = ar[i];   // 12
    i = ar[i];   // 13
    i = ar[i];   // 14
    i = ar[i];   // 15
    i = ar[i];   // 16

    i = ar[i];   // 17
    i = ar[i];   // 18
    i = ar[i];   // 19
    i = ar[i];   // 20
    i = ar[i];   // 21
    i = ar[i];   // 22
    i = ar[i];   // 23
    i = ar[i];   // 24

    i = ar[i];   // 25
    i = ar[i];   // 26
    i = ar[i];   // 27
    i = ar[i];   // 28
    i = ar[i];   // 29
    i = ar[i];   // 30
    i = ar[i];   // 31
    i = ar[i];   // 32

    // Next line included so GCC doesn't optimize the entire function away.
    xor_sum = xor_sum ^ i;
    }

    while( i );

  return xor_sum;
}


int main( int argc, char *argv[], char *env[] ) {

  long int i, step, log_size;
  long int array_size, result;

  clock_t clock_start_time;
  clock_t clock_stop_time;
  clock_t elasped_time;
  double cpu_time;

// For MacOS
  long int * ar = valloc( ARRAY_SIZE * sizeof(long int) );


//  For Linux
//  long int * ar = aligned_malloc( ARRAY_SIZE * sizeof(long int) );

  for( array_size = (1 << LOG_ARRAY_SIZE);
       array_size <= ARRAY_SIZE;
       array_size = array_size + array_size ) {

    printf( "The variable array_size is:  %lu.\n", array_size );

    // Fast-chase initialization
    initialize_fast_chase( ar, array_size );

    // The starting time
    clock_start_time = 0;
    clock_stop_time = 0;

    clock_start_time = clock( );

    // Run the fast-chase tests...
    for( i=0; i < REPEAT; i++ ) result = chase32( ar, 0, 4321 );

    // Print the result so the compiler doesn't optimize away the code
    // we are trying to measure.

    // The ending time
    clock_stop_time = clock( );
    elasped_time = clock_stop_time - clock_start_time;
    cpu_time = ((double) elasped_time) / ((double) CLOCKS_PER_SEC);

    printf( "Fast: result = %8ld,  elasped_time = %8ld,  elapsed %9f.\n",
            result, step, cpu_time );


    // Fast-chase initialization
    initialize_slow_chase( ar, array_size );

    // The starting time
    clock_start_time = 0;
    clock_stop_time = 0;

    clock_start_time = clock( );

    // Run the fast-chase tests...
    for( i=0; i < REPEAT; i++ ) result = chase32( ar, 0, 4321 );

    // Print the result so the compiler doesn't optimize away the code
    // we are trying to measure.

    // The ending time
    clock_stop_time = clock( );
    elasped_time = clock_stop_time - clock_start_time;
    cpu_time = ((double) elasped_time) / ((double) CLOCKS_PER_SEC);

    printf( "Slow: result = %8ld,  elasped_time = %8ld,  elapsed %9f.\n",
            result, step, cpu_time );

   }

//  for( i=0; i < ARRAY_SIZE; i++ )
//    printf( "ar[%3ld] = %3ld.\n", i, ar[i] );

  printf( "Finished.\n" );
}
