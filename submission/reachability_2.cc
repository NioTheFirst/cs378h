//the objective is to compute reachability of a DAG given the initial adjacency matrix.
//the algorithm is as follows:
//INPUT: adjacency matrix A of a DAG with n vertices
//OUTPUT: reachability matrix R of the DAG
//for k = n-1 to 0 do
//    forall edges(k, j) do
//        R[k][j] = 1
//        forall R[j][l] == 1 do
//            R[k][l] = 1
//return R
//Correctness by Induction:
//Base case: k = n-1, All R[k] == 0, since it is a DAG, there is no edge from k to any j, so R[k][j] == 0 for all j, which is correct.
//Induction step: Assume for all k' > k, R[k'] is correct.
//If there exists edge k to node j, we set R[k][j] to 1, which is correct by definition.
//Then, since all nodes reachable from j are reachable now from k, and j > k, by induction hypotheses, R[j] is correct so we set R[k][l] to 1 for all l in R[j], which is correct.
//Runtime:
//The internal loop is costly, assume n. 
//The outer two loop sum to |W| where W is the set of edges in the DAG.
//Total runtime is therefore O(|W| * n).
//we store the input in a file that is passed by name, also we give n as input, and we output the reachability matrix to a file named "reachability.txt" in the same format as the input adjacency matrix, where each line corresponds to a row of the matrix, and entries are separated by spaces.
//Other:
//We would also want to track latency by cycles, with rdtsc
//We don't use the matrix squaring algorithm discussed in class, since the runtime is O(n^3), which is basically worst case of this algorithm.
//
////Observe that my timings of latency may not compare with yours, due to the unrepeatability of runs (too long, unpredictable noise.
//This is my measured latency (in cycles) for a large 2^15 matrix (not turned in due to file size):
//	37213493472
//	37273725404
//	37447746054
//My correctness is essentially testing on many small matrices (64, 128, 256, etc). 2^15 is simply not feasible to verify with the dumb script. I can only assume it is correct from previous correct runs.
//
//TO RUN/TEST
//1) To compile, simple compile with g++ (g++ -O2 reachability.cc -o reach). A precompiled binary is already provided.
//2) You may generate your own matrix with the provided "gen_adj_matrix.cc" fuction. Currently it is configured for spare matrices (1/10 chance of an element is 1). Typical configuration may also be (1/2). For convenience, three medium matrices have been provided (mediu_matrix.txt(64), medium_matrix2.txt(128), medium_matrix_sparse.txt(128, sparse).
//3) Run with the specified input format (i.e. ./reach medium_matrix.txt 64) and the output is stored in reachability.txt
//4) You can compare these matrices with a small python checker, "checker.py". It works through dumb BFS. The command is python3 <adjacency matrix> <reachability matrix> <n>.
//
//
//Optimizations:
//Instead of vector<vector<int>>, we can use a bitset to store the reachability matrix, which can speed up the internal loop by using bitwise operations instead of iterating through each element.
//With bitset OR, we can do many operations in parallel, so W = ceil(|E| / 64), and the internal loop is O(W) instead of O(n), so total runtime is O(|E| * n / 64) in the worst case, which is a significant improvement for large n and sparse graphs.


#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <stdlib.h>
#include <stdio.h>
#include <cstdint>
#include <cstring>
using namespace std;

//Start serialize rdtsc to get latency in cycles 
static inline uint64_t rdtsc_start(void){
    unsigned hi, lo;
    __asm__ __volatile__ (
        "cpuid\n\t"
        "rdtsc\n\t"
        "mov %%edx, %0\n\t"
        "mov %%eax, %1\n\t"
        : "=r" (hi), "=r" (lo)
        :
        : "%rax", "%rbx", "%rcx", "%rdx"
    );
    return ((uint64_t)hi << 32) | lo;
}

//End serialize rdtsc to get latency in cycles
static inline uint64_t rdtsc_end(void){
    unsigned hi, lo;
    __asm__ __volatile__ (
        "rdtscp\n\t"
        "mov %%edx, %0\n\t"
        "mov %%eax, %1\n\t"
        "cpuid\n\t"
        : "=r" (hi), "=r" (lo)
        :
        : "%rax", "%rbx", "%rcx", "%rdx"
    );
    return ((uint64_t)hi << 32) | lo;
}

//[OPT] some array insertion utility for bitset optimization
static inline void set_bit(uint64_t* bitset, int index) {
    bitset[index / 64] |= (1ULL << (index % 64)); // Go to the correct 64-bit word and set the appropriate bit
}

//[OPT] allocate on memalign boundary for bitset optimization
//aligned malloc
void* aligned_malloc(size_t size, size_t alignment) {
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size) != 0) {
        return nullptr;
    }
    return ptr;
}

//[OPT] parser function to read the adjacency matrix from the input file into a bitset format for optimization
uint64_t* parse_input_bitset(const string& filename, int n) {
    int num_words = (n + 63) / 64; // Number of 64-bit words needed to store n bits
    uint64_t* adjacency_matrix = (uint64_t*)aligned_malloc(n * num_words * sizeof(uint64_t), 64);
    if (!adjacency_matrix) {
        cerr << "Error allocating memory for adjacency matrix" << endl;
        exit(1);
    }
    memset(adjacency_matrix, 0, n * num_words * sizeof(uint64_t)); // Initialize to 0

    ifstream infile(filename);
    if (!infile.is_open()) {
        cerr << "Error opening file: " << filename << endl;
        exit(1);
    }
    string line;
    int row = 0;
    while (getline(infile, line) && row < n) {
        istringstream iss(line);
        int value;
        int col = 0;

        while (iss >> value && col < n) {
            if (value == 1) {
                set_bit(adjacency_matrix + row * num_words, col); // Set the bit for this edge
            }
            col++;
        }

        if (col < n) {
            cerr << "Error: Not enough values in line " << (row + 1) 
                 << ". Expected " << n << " values, got " << col << endl;
            exit(1);
        }
        row++;
    }

    if (row < n) {
        cerr << "Error: Not enough rows in file. Expected " << n 
             << " rows, got " << row << endl;
        exit(1);
    }

    return adjacency_matrix;
}

//Parser function to read the adjacency matrix from the input file
vector<vector<int>> parse_input(const string& filename, int n) {
    vector<vector<int>> adjacency_matrix(n, vector<int>(n, 0)); //Initialize n by n 0 matrx
    ifstream infile(filename);
    if (!infile.is_open()) {
        cerr << "Error opening file: " << filename << endl;
        exit(1);
    }
    string line;
    int row = 0;
    while (getline(infile, line) && row < n) {
        istringstream iss(line);
        int value;
        int col = 0;
        
        // Read values from the line, but only take the first n values
        while (iss >> value && col < n) {
            adjacency_matrix[row][col] = value;
            col++;
        }
        
        // Check if we got enough values for this row
        if (col < n) {
            cerr << "Error: Not enough values in line " << (row + 1) 
                 << ". Expected " << n << " values, got " << col << endl;
            exit(1);
        }
        
        row++;
    }
    
    if (row < n) {
        cerr << "Error: Not enough rows in file. Expected " << n 
             << " rows, got " << row << endl;
        exit(1);
    }
    
    return adjacency_matrix;
}

//[OPT]Function to compute the reachability matrix using the given algorithm, optimized with bitsets
uint64_t* compute_reachability_bitset(const uint64_t* A, int n) {
    int W = (n + 63) / 64;

    uint64_t* R = (uint64_t*)aligned_malloc((size_t)n * W * sizeof(uint64_t), 64);
    if (!R) { perror("alloc R"); exit(1); }

    memcpy(R, A, (size_t)n * W * sizeof(uint64_t)); // R starts as adjacency

    for (int k = n - 1; k >= 0; k--) {
        uint64_t* Rk = R + (size_t)k * W;
        const uint64_t* Ak = A + (size_t)k * W;

        // scan words that could contain j > k
        for (int w = (k >> 6); w < W; w++) {
            uint64_t bits = Ak[w];

            // In the first word, mask off bits for j <= k (diagonal + below)
            if (w == (k >> 6)) {
                int bit_in_word = k & 63;
                uint64_t mask = ~((1ULL << (bit_in_word + 1)) - 1); // keep only positions > k
                bits &= mask;
            }

            while (bits) {
                unsigned b = (unsigned)__builtin_ctzll(bits);
                int j = (w << 6) + (int)b;    // j = 64*w + b
                bits &= bits - 1;             // clear lowest set bit

                uint64_t* Rj = R + (size_t)j * W;

                // R[k] |= R[j]
                for (int word = 0; word < W; word++) {
                    Rk[word] |= Rj[word];
                }
            }
        }
    }
    return R;
}

//Function to compute the reachability matrix using the given algorithm
vector<vector<int>> compute_reachability(const vector<vector<int>>& adjacency_matrix, int n){
    vector<vector<int>> reachability_matrix = adjacency_matrix; //Initialize reachability matrix as the adjacency matrix
    for (int k = n - 1; k >= 0; k--) {
        for (int j = 0; j < n; j++){
            if(adjacency_matrix[k][j] == 1){
                //there exists edge k, j
                reachability_matrix[k][j] = 1; //Set R[k][j] to 1
                for (int l = 0; l < n; l++){
                    if (reachability_matrix[j][l] == 1){
                        reachability_matrix[k][l] = 1; //Set R[k][l] to 1 for all l in R[j]
                    }
                }
            }
        }
    }
    return reachability_matrix;
}

//main
int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <input_file> <n>" << endl;
        return 1;
    }
    string input_file = argv[1];
    int n = atoi(argv[2]);
    
    //vector<vector<int>> adjacency_matrix = parse_input(input_file, n);
    uint64_t* adjacency_matrix = parse_input_bitset(input_file, n); //opt
    
    uint64_t start_cycles = rdtsc_start();
    //vector<vector<int>> reachability_matrix = compute_reachability(adjacency_matrix, n);
    uint64_t* reachability_matrix = compute_reachability_bitset(adjacency_matrix, n); //opt
    uint64_t end_cycles = rdtsc_end();
    
    //Output the reachability matrix to a file
    /*ofstream outfile("reachability.txt");
    for (const auto& row : reachability_matrix) {
        for (size_t i = 0; i < row.size(); i++) {
            outfile << row[i] << (i < row.size() - 1 ? " " : "\n");
        }
    }*/
    
    cout << "Latency in cycles: " << (end_cycles - start_cycles) << endl;

    ofstream outfile("reachability.txt");

    int num_words = (n + 63) / 64;

    for (int i = 0; i < n; i++) {
        uint64_t* row = reachability_matrix + i * num_words;

        for (int j = 0; j < n; j++) {
            int bit = (row[j / 64] >> (j % 64)) & 1;

            outfile << bit;
            if (j < n - 1)
                outfile << " ";
        }

        outfile << "\n";
    }
    
    return 0;
}
