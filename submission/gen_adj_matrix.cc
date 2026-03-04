//generates a random nxn adjacency matrix with 0.5 probability of an edge between any two nodes
#include <iostream>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <fstream> 

using namespace std;

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 3) {
        cerr << "Usage: " << argv[0] << " <size_of_matrix> {<file name>}" << endl;
        return 1;
    }

    int n = atoi(argv[1]);
    srand(time(0)); // Seed the random number generator

    vector<vector<int>> adjacency_matrix(n, vector<int>(n, 0));

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            if (i < j) { // No self-loops
                adjacency_matrix[i][j] = (rand() % 10 == 0); // sparse matrix: 1/10
            }
        }
    }

    // Print the adjacency matrix
    /*for (const auto& row : adjacency_matrix) {
        for (const auto& val : row) {
            cout << val << " ";
        }
        cout << endl;
    }*/

    if (argc == 3) {
        ofstream outfile(argv[2]);
        if (!outfile) {
            cerr << "Error opening file: " << argv[2] << endl;
            return 1;
        }
        // Write the adjacency matrix to the file
        for (const auto& row : adjacency_matrix) {
            for (const auto& val : row) {
                outfile << val << " ";
            }
            outfile << endl;
        }
        outfile.close();
    }

    return 0;
}
