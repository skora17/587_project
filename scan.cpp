#include <iostream>
#include <vector>
#include <set>
#include <map>


// Corresponds to Lemma 3.2


std::vector<int> bfs_array(const std::vector<std::vector<int>>& adj, int s, int L) { // could be recursive
    int n = adj.size();
    

    std::vector<int> dist(n, L + 1);

    // S(0), S(1), ..., S(L)
    std::vector<std::set<int>> levels(L+1);

    // Initialize
    dist[s] = 0;
    levels[0].insert(s);

    // BFS by levels up to depth L
    // levels are sequential
    for (int i = 0; i < L; ++i) {
        const std::set<int>& curr = levels[i];
        std::set<int>& next = levels[i + 1];


        if (curr.empty()) {
            break;
        }

        // within level we parallelize expaniding the fringe

        
        for (int v : curr) {  // iterate over nodes
            for (int u : adj[v]) {  // iterate over out-neighbors
                if (dist[u]>i+1) {
                    dist[u]=i+1;
                    next.insert(u);  // critical?
                }
            }
        }
    }
    return dist;
}







int main() {
    // Example graph:
    //
    // 0 -> 1
    // v    v
    // 2 -> 3
    // v    v
    // 4    5

    int n = 6;
    std::vector<std::vector<int>> adj(n);
    auto add_edge = [&](int u, int v) {
        adj[u].push_back(v);
    };

    add_edge(0, 1);
    add_edge(0, 2);
    add_edge(1, 3);
    add_edge(2, 3);
    add_edge(2, 4);
    add_edge(3, 5);


    auto res = bfs_array(adj, 0, 2);

    std::cout << "Dist array (L = 2):\n";
    for (int v = 0; v < n; ++v) {
        std::cout << "  v = " << v << ", Dist[v] = " << res[v] << "\n";
    }

    return 0;
}
