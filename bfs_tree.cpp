#include <iostream>
#include <vector>
#include <set>
#include <functional>


template <typename T>
class PriorityStructure;

// Theorem 1.2 Data Structure //

class DynamicSSSP {
public:
    DynamicSSSP(const std::vector<std::vector<int>>& adjOut, int s, int L)
        : n(adjOut.size()), L(L), s(s), Dist(),
        Out(adjOut), In(),
        Scan(), T(), Parent()
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

        for (auto [u, v] : delEdges) { // iterate over delete batch

            long long key = encodeEdge(u, v);
            auto it = alive.find(key);
            if (it == alive.end()) {
                continue; // skip if already deleted
            }

            alive.erase(it); // really need to tell all the data structures (IN/OUT) that this edge is dead

            // If the edge ei does not belong to T, we remove it from the data structure In(v) and Out(v)
            // by marking it as an invalid edge. This can be done with a single call of the Set operation
            // per each edge, requiring an O(1) work and depth. Hence, we are left only with edges from T.


            if (T[v] == u) { // removed a tree edge


                // Then, we remove all edges from T and update the pointer Scan(v) to point to the next edge from In(v),
                // using the NextWith function. This might cause the Scan(v) to point to the end of the list,
                // which does not correspond to any actual edges.

                // update SCAN using NEXTWITH
                auto &children = T[u];
                children.erase(std::remove(children.begin(), children.end(), v), children.end());
                parentDeleted[v] = true;
            }

        }

        // TODO CHECK below
        // // initialize U to be the empty set
        // std::unsortedset<int> U;

        
        // for (int v = 0; v < n; ++v) {
        //     if (parentDeleted[v] && Dist[v] >= 1 && Dist[v] <= L) {
        //         U.push_back(v);
        //     }
        // }

        // if (U.empty()) return;

        // ---- Phases i = 0..L (Algorithm 1 lines 4–15) ----
        for (int i = 0; i <= L && !U.empty(); ++i) {
            std::unsortedset<int> Unew;

            // parallel loop line 6-11
            for (int v : U) {
                if (Dist[v] > L) continue;
                if (Dist[v] < i) continue;

                // predicate for NextWith
                auto predicate = [this, v](const int& w) -> bool {
                    if (Dist[w] != Dist[v] - 1) return false;
                    long long key = encodeEdge(w, v);
                    return alive.find(key) != alive.end();
                };

                int start = Scan[v];
                if (start < 1) start = 1;

                int pos = In[v].nextWith(start, predicate);
                int sz  = In[v].size();

                if (pos >= 1 && pos <= sz) {
                    // Found a new parent at same distance
                    int newParent = In[v].query(pos);

                    // Remove v from old parent’s children (if any)
                    if (Parent[v] != -1) {
                        auto &oldKids = T[Parent[v]];
                        oldKids.erase(std::remove(oldKids.begin(), oldKids.end(), v),
                                    oldKids.end());
                    }

                    Parent[v] = newParent;
                    Scan[v]   = pos;
                    T[newParent].push_back(v);

                    // Dist[v] stays as-is. v leaves U (we simply don't add it to Unew).
                    parentDeleted[v] = false;
                } else {
                    // No edge (w -> v) with Dist(w) = Dist[v]-1 remains.
                    // Its distance must increase and its subtree may be broken.
                    Scan[v] = 1;  // restart scanning from beginning next time

                    if (!inUnew[v]) {
                        Unew.push_back(v);
                        inUnew[v] = true;
                    }

                    // Add all children of v in T to Unew (their distances may now be wrong).
                    for (int child : T[v]) {
                        if (!inUnew[child]) {
                            Unew.push_back(child);
                            inUnew[child] = true;
                        }
                        parentDeleted[child] = true;
                    }

                    // v loses all children until they get reattached later
                    T[v].clear();
                }
            }

            // line 12
            for (int v = 0; v < n; ++v) {
                if (Dist[v] == i && parentDeleted[v]) {
                    if (!inUnew[v]) {
                        Unew.push_back(v);
                        inUnew[v] = true;
                    }
                }
            }

            // line 13
            U.swap(Unew);

            // parallel loop line 14-15
            for (int v : U) {
                Dist[v] = i + 1;
            }

        }
    }


private:
    int n;
    int L;
    int s;
    std::vector<int> Dist;
    std::vector<std::vector<int>> Out;
    std::vector<PriorityStructure<int>> In;

    std::vector<int> Scan;
    std::vector<int> T;

    std::unordered_set<long long> alive;

    static long long encodeEdge(int u, int v) {
        return (static_cast<long long>(u) << 32) ^
            static_cast<unsigned int>(v);
    }



    void initScanAndTree() {
        Scan.assign(n, 0);
        Parent.assign(n, -1);
        T.assign(n, std::vector<int>());

        for (int v = 0; v < n; v++) {

            int d = Dist[v];
            if (d == 0 || d > L) {
                continue;
            }

            auto predicate = [this, v](const int& w) -> bool {
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
                T[w].push_back(v);
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
    //
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

    dsssp.debugPrint();

    return 0;
}
