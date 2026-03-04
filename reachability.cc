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

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <stdlib.h>
#include <stdio.h>
#include <cstdint>

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
    
    vector<vector<int>> adjacency_matrix = parse_input(input_file, n);
    
    uint64_t start_cycles = rdtsc_start();
    vector<vector<int>> reachability_matrix = compute_reachability(adjacency_matrix, n);
    uint64_t end_cycles = rdtsc_end();
    
    //Output the reachability matrix to a file
    ofstream outfile("reachability.txt");
    for (const auto& row : reachability_matrix) {
        for (size_t i = 0; i < row.size(); i++) {
            outfile << row[i] << (i < row.size() - 1 ? " " : "\n");
        }
    }
    
    cout << "Latency in cycles: " << (end_cycles - start_cycles) << endl;
    
    return 0;
}
