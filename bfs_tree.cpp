#include <iostream>
#include <vector>
#include <set>
#include <functional>
#include <unordered_set>
#include <algorithm>


template <typename T>
class PriorityStructure;

std::vector<int> bfs_array(const std::vector<std::vector<int>>& adj, int s, int L) {
    int n = adj.size();

    std::vector<int> dist(n, L + 1);

    std::vector<std::set<int>> levels(L+1);

    // Initialize
    dist[s] = 0;
    levels[0].insert(s);

    // BFS by levels up to depth L
    for (int i = 0; i < L; ++i) {
        const std::set<int>& curr = levels[i];
        std::set<int>& next = levels[i + 1];

        if (curr.empty()) {
            break;
        }

        // within level we parallelize expanding the fringe (conceptually)
        for (int v : curr) {  // iterate over nodes
            for (int u : adj[v]) {  // iterate over out-neighbors
                if (dist[u] > i + 1) {
                    dist[u] = i + 1;
                    next.insert(u);
                }
            }
        }
    }
    return dist;
}


// Theorem 1.2 Data Structure //

class DynamicSSSP {
public:
    DynamicSSSP(const std::vector<std::vector<int>>& adjOut, int s, int L)
        : n(adjOut.size()), L(L), s(s), Dist(),
          Out(adjOut), In(),
          Scan(), Tv(), Parent(),
          alive()
    {
        // 1) Dist via Lemma 3.2:
        Dist = bfs_array(Out, s, L);

        // 2) Build In(v) as PriorityStructure using in-neighbors:
        int maxPriority = n;  // priorities in [1..n]
        std::vector<std::vector<int>> adjOutInv(n);
        for (int u = 0; u < n; u++) {
            for (int v : adjOut[u]) {
                adjOutInv[v].push_back(u);
            }
        }

        In.reserve(n);
        for (int v = 0; v < n; v++) {
            std::vector<std::pair<int,int>> elems;
            elems.reserve(adjOutInv[v].size());
            for (int u : adjOutInv[v]) {
                // value = u, priority = u+1 (must be in [1..maxPriority])
                elems.emplace_back(u, u+1);
            }
            In.emplace_back(maxPriority);
            In[v].initialize(elems);
        }

        // 3) Initialize alive-edge set
        for (int u = 0; u < n; ++u) {
            for (int v : Out[u]) {
                alive.insert(encodeEdge(u, v));
            }
        }

        // 4) Initialize Scan, Parent, T to form the initial BFS tree T
        initScanAndTree();
    }


    // page 10 - Algorithm 1
    void batchDelete(const std::vector<std::pair<int,int>>& delEdges) {
        std::vector<bool> parentDeleted(n, false);
        std::vector<std::pair<int,int>> treeEdges;   // edges from T whose parent is removed

        // First pass
        for (auto [u, v] : delEdges) { // iterate over delete batch
            if (u < 0 || u >= n || v < 0 || v >= n) continue;

            long long key = encodeEdge(u, v);
            auto it = alive.find(key);
            if (it == alive.end()) {
                continue;
            }

            alive.erase(it); // tell the data structure this edge is dead
            auto &outList = Out[u];
            outList.erase(std::remove(outList.begin(), outList.end(), v), outList.end());

            // If the edge ei does not belong to T, we remove it from the data structure In(v) and Out(v)
            // by marking it as an invalid edge. This can be done with a single call of the Set operation
            // per each edge, requiring an O(1) work and depth. Hence, we are left only with edges from T.

            if (Parent[v] == u) { // parent deleted (tree edge)
                treeEdges.emplace_back(u, v);
                parentDeleted[v] = true;
                // Remove v from children list Tv[u]
                auto &kids = Tv[u];
                kids.erase(std::remove(kids.begin(), kids.end(), v), kids.end());
                Parent[v] = -1;
            }
        }

        // Second Pass
        for (auto [u, v] : treeEdges) {

            // predicate for NextWith
            auto predicate = [this, v](const int& w) -> bool {
                if (Dist[w] != Dist[v] - 1) return false;
                long long key = encodeEdge(w, v);
                return alive.find(key) != alive.end();
            };

            Scan[v] = In[v].nextWith(Scan[v], predicate);

            if (Scan[v] != In[v].size()+1) {
                int w = In[v].query(Scan[v]);
                Parent[v] = w;
                Tv[w].push_back(v);
                parentDeleted[v] = false;
            }
        }

        std::unordered_set<int> U;  // Algorithm 1 line 3


        // ---- Phases i = 0..L (Algorithm 1 lines 4–15) ----
        // Phase i:     "resolve" any vertices in U whose true distance is exactly i
        //              add to U any vertices who may have distance i+1 but incorrectly recorded
        // Invariants:  any vertex whose true distance is AT MOST i is either in U or already resolved
        //              U contains only elements of distance at least i
        //              Every element of U has its distance marked as i  (in the ideal version)
        for (int i = 0; i <= L; i++) {
            std::unordered_set<int> Unew;

            // parallel loop line 6-11
            for (int v : U) {

                // predicate for NextWith
                auto predicate = [this, v](const int& w) -> bool {
                    if (Dist[w] != Dist[v] - 1) return false;
                    long long key = encodeEdge(w, v);
                    return alive.find(key) != alive.end();
                };

                // Line 7: rescan from current Scan(v)
                Scan[v] = In[v].nextWith(Scan[v], predicate);

                if (Scan[v] == In[v].size() + 1) {
                    // Line 9
                    Scan[v] = 1;

                    // Line 10
                    Unew.insert(v);

                    // Line 11
                    for (int child : Tv[v]) {
                        Unew.insert(child);
                    }
                    Tv[v] = std::vector<int>();

                } else {
                    int w = In[v].query(Scan[v]);
                    Parent[v] = w;
                    Tv[w].push_back(v);
                }
            }

            // line 12
            for (int v=0; v<n; v++) {  // Linear complexity... corrct impl??
                if (Dist[v] == i + 1 && parentDeleted[v]) {
                    Unew.insert(v);
                }  // sufficient for decremental, can simply throw away nodes after they get too far.
            }

            // line 13
            U.swap(Unew);

            // parallel loop line 14-15
            for (int v : U) {
                Dist[v] = i + 1;
            }
        }
    }



    void debugPrint() const {
        std::cout << "Dist:\n";
        for (int v = 0; v < n; ++v) {
            std::cout << "   Dist[" << v << "] = " << Dist[v] << "\n";
        }

        std::cout << "\nParent (tree T):\n";
        for (int v = 0; v < n; ++v) {
            std::cout << v << " -> " << Parent[v] << "\n";
        }
    }


private:
    int n;
    int L;
    int s;
    std::vector<int> Dist;
    std::vector<std::vector<int>> Out;
    std::vector<PriorityStructure<int>> In;

    std::vector<int> Scan;              // Scan(v) represents RANKs within In(v)
    std::vector<int> Parent;            // T, represented by parent map
    std::vector<std::vector<int>> Tv;   // T, represented by child map     

    std::unordered_set<long long> alive;

    static long long encodeEdge(int u, int v) {
        return (static_cast<long long>(u) << 32) ^
               static_cast<unsigned int>(v);
    }

    void initScanAndTree() {
        Scan.assign(n, 0);
        Parent.assign(n, -1);
        Tv.assign(n, std::vector<int>());

        for (int v = 0; v < n; v++) {

            int d = Dist[v];
            if (d == 0 || d > L) {
                continue;
            }

            auto predicate = [this, v](const int& w) -> bool {
                if (w < 0 || w >= n) return false;
                if (Dist[w] != Dist[v] - 1) return false;
                long long key = encodeEdge(w, v);
                return alive.find(key) != alive.end();
            };

            int pos = In[v].nextWith(1, predicate);
            int sz  = In[v].size();

            if (pos >= 1 && pos <= sz) {
                int w = In[v].query(pos);
                Scan[v]   = pos;
                Parent[v] = w;
                Tv[w].push_back(v);
            } else {
                Scan[v] = sz + 1;
                Parent[v] = -1;
            }
        }
    }
};


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

    int s = 0;
    int L = 3;

    // Construct the Theorem 1.2 data structure
    DynamicSSSP dsssp(adj, s, L);

    std::cout << "Initial structure:\n";
    dsssp.debugPrint();

    // Example batch deletion
    std::vector<std::pair<int,int>> delEdges = {{2,3}};
    dsssp.batchDelete(delEdges);

    std::cout << "\nAfter batchDelete({(2,3)}):\n";
    dsssp.debugPrint();



    // Cycle

    n = 5;
    adj.assign(n, std::vector<int>());

    add_edge(0, 1);
    add_edge(1, 0);
    add_edge(0, 4);
    add_edge(4, 0);
    add_edge(2, 1);
    add_edge(1, 2);
    add_edge(2, 3);
    add_edge(3, 2);
    add_edge(4, 3);
    add_edge(3, 4);

    s = 0;
    L = 3;

    // Construct the Theorem 1.2 data structure
    DynamicSSSP dsssp2(adj, s, L);

    std::cout << "Initial structure:\n";
    dsssp2.debugPrint();

    // Example batch deletion
    delEdges = {{0,1}, {1,0}};
    dsssp2.batchDelete(delEdges);

    std::cout << "\nAfter batchDelete({(0,1)}):\n";
    dsssp2.debugPrint();

    return 0;
}
