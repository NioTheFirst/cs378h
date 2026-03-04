#!/usr/bin/env python3
import sys
from collections import deque

def read_matrix(path: str, n: int):
    M = []
    with open(path, "r") as f:
        for i in range(n):
            line = f.readline()
            if not line:
                raise ValueError(f"File ended early at row {i}")
            row = line.strip().split()
            if len(row) < n:
                raise ValueError(f"Row {i} has {len(row)} entries, expected {n}")
            M.append([int(x) for x in row[:n]])
    return M

def closure_by_bfs(A):
    """Naive transitive closure: for each source s, BFS/DFS through adjacency."""
    n = len(A)
    # Build adjacency list for speed
    adj = [[] for _ in range(n)]
    for i in range(n):
        # keep all edges with A[i][j]==1
        adj[i] = [j for j, v in enumerate(A[i]) if v == 1]

    R = [[0]*n for _ in range(n)]

    for s in range(n):
        seen = [False]*n
        q = deque()

        # start from direct neighbors
        for v in adj[s]:
            if not seen[v]:
                seen[v] = True
                q.append(v)

        while q:
            u = q.popleft()
            R[s][u] = 1
            for v in adj[u]:
                if not seen[v]:
                    seen[v] = True
                    q.append(v)

    return R

def compare_matrices(R_ref, R_out, max_mismatches=20):
    n = len(R_ref)
    mismatches = 0
    for i in range(n):
        for j in range(n):
            if R_ref[i][j] != R_out[i][j]:
                if mismatches < max_mismatches:
                    print(f"Mismatch at ({i},{j}): expected {R_ref[i][j]}, got {R_out[i][j]}")
                mismatches += 1
    return mismatches

def main():
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <adjacency.txt> <reachability.txt> <n>")
        sys.exit(1)

    adj_path = sys.argv[1]
    out_path = sys.argv[2]
    n = int(sys.argv[3])

    if not (64 <= n <= 128):
        print("Warning: this checker is intended for n ~ 64..128 (but will work for other small n).")

    A = read_matrix(adj_path, n)
    R_out = read_matrix(out_path, n)

    print("Computing reference reachability (BFS per node)...")
    R_ref = closure_by_bfs(A)

    print("Comparing...")
    mismatches = compare_matrices(R_ref, R_out)
    if mismatches == 0:
        print("OK: reachability.txt matches reference.")
        sys.exit(0)
    else:
        print(f"FAIL: {mismatches} mismatches found.")
        sys.exit(2)

if __name__ == "__main__":
    main()
