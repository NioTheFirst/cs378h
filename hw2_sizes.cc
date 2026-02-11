//cache_data_sizes.cc
/*
This code asnwers part 1 and part 2 of the lab, dealing with finding the dcache of the l1d, l2, and l3 cache sizes.

To compile/build: g++ -O2 cache_data_sizes.cc -o cache_data_sizes

Then, we want to pin on one core (to minimize the effects of multi-core usage:
taskset -c 0 ./cache_data_sizes p1
taskset -c 0 ./cache_data_sizes p2 --linesize 64 (or whatever the line size was measured for part 1).
*/


#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>
#include <string>
#include <iostream>

#include <x86intrin.h>

//Get the cycles (used for timing
//first time using cmdline ubuntu, why is the tab all messed up
static inline uint64_t rdtsc_serialized() {
    _mm_lfence();
    uint64_t t = __rdtsc();
    _mm_lfence();
    return t;
}

//static constants
static constexpr size_t kPageSize = 4096;
static constexpr size_t kBufSize  = 64ull * 1024 * 1024; // 64MB
static constexpr int kLoadsP1     = 256;


//linked list data structure to move memory loads
struct Node {
  Node* next;
  uint64_t data;
};


// HELPER FUNCTIONS==================================================================================================================================================


//allocate on page alignment.
static uint8_t* alloc_page_aligned(size_t bytes) {
  void* p = nullptr;

  if (posix_memalign(&p, kPageSize, bytes) != 0) return nullptr; //can't

  std::memset(p, 0, bytes); //0 out

  return reinterpret_cast<uint8_t*>(p);
}

//similar to print (keep a pointer alive). Taken from a friend's advice (Rohan, Graduate Student)
static inline void keep_live(const void* p) {
  asm volatile("" : : "r"(p) : "memory");
}

//tash cache: clear all.
static void trash_caches(const uint8_t* buf, size_t bytes) {
  volatile uint64_t sum = 0;
  const uint64_t* p = reinterpret_cast<const uint64_t*>(buf);
  size_t n = bytes / sizeof(uint64_t);

  for (size_t i = 0; i < n; i++){
    sum += p[i];
  }
  (void)sum;
}

//construct the linked list.
static Node* build_list(uint8_t* base, size_t bytes, size_t stride, bool scrambled) {
  if (stride < sizeof(Node)){
    std::cout << "[!] Stride < node size. Please fix\n";
    return nullptr; //sanity check
  }

  size_t count = bytes / stride; //how many
  if (count < 2) return nullptr;

  std::vector<uint32_t> idx(count);
  for (uint32_t i = 0; i < count; i++) idx[i] = i;

  //we scramble as in 3.7b here.
  //to do this, we define a super duper random number:
  if (scrambled) {
    //yay cofee. We generate a seed
    uint64_t seed = 0xC0FFEEULL ^ (uint64_t)count ^ (uint64_t)stride;
    //then, we define a hash lambda
    auto lcg = [&seed]() -> uint32_t {
      seed = seed * 0xDEADBEEFDEADBEEFULL + 1442695040888963407ULL;
      return (uint32_t)(seed >> 32);
    };
    //we apply the swap. std::swap swaps the values.
    for (size_t i = count - 1; i > 0; i--) {
      size_t j = (size_t)(lcg() % (uint32_t)(i + 1));
      std::swap(idx[i], idx[j]);
    }
    //source: gpt. I think this is fine, since it is simply generating a random number.
    //this is called a Fisher-Yates shuffle
    // id ends up like [0, 13, 4, 26, 2, 100, ...]
    //debug

    /*for (int i = 0; i < count; i++){
      std::cout << "[D] Shuffle[" << i << "]: " << idx[i] << "\n";
    }*/
  }

  //3.10 DRAM tricking.
  constexpr size_t kExtraBit = (1u << 14); // 16KB
  Node* first = reinterpret_cast<Node*>(base + idx[0] * stride);
  Node* cur = first;

  //hell yes i get to code reuse yay!

  for (size_t k = 1; k < count; k++) {
    size_t off = idx[k] * stride;
    //if (scrambled && (k & 1)) off ^= kExtraBit;
    Node* nxt = reinterpret_cast<Node*>(base + off);
    cur->next = nxt;
    cur->data = 0x12345678ULL; //some bullshit data
    cur = nxt;
  }
  cur->next = first; // cycle
  cur->data = 0x12345678ULL;
  return first;
}


// ---------- Problem 1 timing loops ----------

//1a) naive version. We simply time the easy stuff
static uint64_t p1_naive_cy_per_load(uint8_t* base, size_t bytes, size_t stride) {
  const uint64_t* p = reinterpret_cast<const uint64_t*>(base);
  size_t step = stride / sizeof(uint64_t);
  volatile uint64_t sum = 0;

  //trash the cache
  trash_caches(base, bytes);

  //begin timing
  uint64_t t0 = rdtsc_serialized();
  const uint64_t* q = p;
  for (int i = 0; i < kLoadsP1; i += 4) {
    //a bit of loop unrolling, courtesy of hw0
    sum += q[0 * step];
    sum += q[1 * step];
    sum += q[2 * step];
    sum += q[3 * step];
    q += 4 * step;
  }
  uint64_t t1 = rdtsc_serialized();
  (void)sum;
  //i'm too lazy to remove the loop overhead. I'd do it if i didn't have an exam ;-;
  return (t1 - t0) / kLoadsP1;
}

//1b) dependent cycles load. Now we use our build_list to make a linked list bfor the dependent cycles bullshit
static uint64_t p1_dep_cy_per_load(uint8_t* base, size_t bytes, size_t stride, bool scrambled) {
  //scrambled gives us the option to get 1c) Praise be code reuse, saving time and money
  Node* head = build_list(base, bytes, stride, scrambled);
  if (!head) return 0;

  //tash the caches
  trash_caches(base, bytes);

  Node* p = head;
  //begin timing
  uint64_t t0 = rdtsc_serialized();
  for (int i = 0; i < kLoadsP1; i += 4) {
    p = p->next;
    p = p->next;
    p = p->next;
    p = p->next;
  }
  uint64_t t1 = rdtsc_serialized();
  keep_live(p);
  return (t1 - t0) / kLoadsP1;
}

static void run_p1(uint8_t* buf) {
  std::puts("== P1: stride sweep (naive vs dep-linear vs dep-scrambled) ==");
  std::puts("strideB\tnaive_cy/ld\tlinear_dep_cy/ld\tscrambled_dep_cy/ld");

  const size_t working = 64 * 1024;   // << key change: small working set for P1

  for (int lg = 4; lg <= 12; lg++) {
    size_t stride = 1ull << lg; // 16..4096
    uint64_t naive = p1_naive_cy_per_load(buf, kBufSize, stride);
    uint64_t lin   = p1_dep_cy_per_load(buf, working, stride, /*scrambled=*/false);
    uint64_t scr   = p1_dep_cy_per_load(buf, working, stride, /*scrambled=*/true);
    std::printf("%zu\t%llu\t%llu\t%llu\n",
                stride,
                (unsigned long long)naive,
                (unsigned long long)lin,
                (unsigned long long)scr);
  }
  std::puts("\nHint: line size is typically the smallest stride where scrambled_dep stops improving.");
}

// ---------- Problem 2 cache-size sweep ----------

//Ok, my goal idea is this. I'm going to build a single big pointer chain over the whole buffer with strid = linesize (64 bytes)
//Basically, I repeatedly pass over these buffers, once to warmup the cache, then 3 times for consistent measurement.


static uint64_t dep_walk_cy_per_load(Node* head, int count) {
  Node* p = head;
  uint64_t t0 = rdtsc_serialized();
  for (int i = 0; i < count; i += 4) {
    p = p->next;
    p = p->next;
    p = p->next;
    p = p->next;
  }
  uint64_t t1 = rdtsc_serialized();
  keep_live(p);
  return (t1 - t0) / (uint64_t)count;
}

static void run_p2(uint8_t* buf, int linesize) {
  std::puts("== P2: cache capacity sweep (scrambled dependent) ==");
  std::printf("linesize=%d bytes\n", linesize);
  std::puts("lgcount\tlines\tbytes\tcy/ld_cold\tcy/ld_w1\tcy/ld_w2\tcy/ld_w3");

  Node* head = build_list(buf, kBufSize, (size_t)linesize, /*scrambled=*/true);
  if (!head) {
    std::fprintf(stderr, "failed to build list\n");
    return;
  }

  // sweep working set sizes in cache lines: 2^lgcount lines
  for (int lg = 4; ; lg++) {
    int lines = 1 << lg;
    size_t bytes = (size_t)lines * (size_t)linesize;
    if (bytes > kBufSize) break;

    trash_caches(buf, kBufSize);

    uint64_t r0 = dep_walk_cy_per_load(head, lines); // cold
    uint64_t r1 = dep_walk_cy_per_load(head, lines); // warm
    uint64_t r2 = dep_walk_cy_per_load(head, lines); // warm
    uint64_t r3 = dep_walk_cy_per_load(head, lines); // warm

    std::printf("%d\t%d\t%zu\t%llu\t%llu\t%llu\t%llu\n",
                lg, lines, bytes,
                (unsigned long long)r0,
                (unsigned long long)r1,
                (unsigned long long)r2,
                (unsigned long long)r3);
  }

  std::puts("\nInterpretation: ignore cold. Use median(w1,w2,w3). Step-ups indicate L1d->L2->L3->DRAM.");
}

static void usage(const char* argv0) {
  std::fprintf(stderr,
    "Usage: %s p1 | p2 [--linesize N]\n"
    "  p1: stride sweep (naive/linear/scrambled)\n"
    "  p2: cache capacity sweep (L1d/L2/L3)\n", argv0);
}

int main(int argc, char** argv) {
  if (argc < 2) { usage(argv[0]); return 1; }

  uint8_t* buf = alloc_page_aligned(kBufSize);
  if (!buf) { std::fprintf(stderr, "alloc failed\n"); return 1; }

  std::string mode = argv[1];

  if (mode == "p1") {
    run_p1(buf);
  } else if (mode == "p2") {
    int linesize = 64;
    for (int i = 2; i < argc; i++) {
      if (std::string(argv[i]) == "--linesize" && i + 1 < argc) {
        linesize = std::atoi(argv[++i]);
      }
    }
    run_p2(buf, linesize);
  } else {
    usage(argv[0]);
    return 1;
  }

  std::free(buf);
  return 0;
}
