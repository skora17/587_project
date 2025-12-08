#include <vector>
#include <functional>
#include <stdexcept>
#include <iostream>
#include <string>
#include <algorithm>
#include <omp.h>


// thread aligned subtrees

template <typename T>
class PriorityStructure {
public:
    explicit PriorityStructure(int maxPriority)
        : maxP(maxPriority), root(nullptr) {}

    // **API FUNCTION**
    // INITIALIZE({(v1, p1), ..., (vl, pl)})
    // initialize segment tree from list of (value, priority) pairs
    void initialize(const std::vector<std::pair<T,int>>& elems) {
        // TODO: in case we ever call initialize multiple times, need to delete original tree
        root = nullptr;

        if (elems.empty()) {
            return;
        }

        // Keep convention: (value, priority)
        std::vector<std::pair<T,int>> items = elems;

        // sort by priority = .second
        std::sort(items.begin(), items.end(),
                  [](const auto& a, const auto& b) {
                      return a.second < b.second;  // sort by priority
                  });

        // Decide how deep we allow parallel tasking.
        // At depth d, there are at most 2^d subtrees. We cap d so 2^d <= numThreads.
        int numThreads = omp_get_max_threads();
        int maxParallelDepth = 0;
        while ((1 << maxParallelDepth) < numThreads) {
            ++maxParallelDepth;
        }
        

        #pragma omp parallel
        {
            #pragma omp single
            {
                root = buildFromSorted(items, 0, 1, maxP, 0, maxParallelDepth);
            }
        }
    }

    // number of elements currently stored
    int size() const {
        if (root) {
            return root->cnt;
        } else {
            return 0;
        }
    }

    // **API FUNCTION**
    // QUERY(k)
    // return the element with k-th largest priority
    T query(int k) const {
        int n = size();
        if (k < 1 || k > n) {
            throw std::out_of_range("QUERY: k out of range");
        }
        return queryByRank(root, 1, maxP, k);
    }

    // **API FUNCTION**
    // UPDATEVALUE(k, v)
    // update the value of the element with k-th largest priorit to v.
    void updateValue(int k, const T& v) {
        int n = size();
        if (k < 1 || k > n) {
            throw std::out_of_range("updateValue: k out of range");
        }

        updateValueHelper(root, 1, maxP, k, v);
        return;
    }

    // **API FUNCTION**
    // FIND(p)
    // return the rank (k) and value (v) of the element with priority p
    std::pair<T,int> find(int p) const {
        if (p < 1 || p > maxP) {
            // ERROR
            throw std::out_of_range("find: priority out of range");
        }
        int rank = 0;
        return findByPriority(root, 1, maxP, p, rank);
    }

    // **API FUNCTION**
    // UPDATEPRIORITY(k, p)
    // change the priority of the element with k-th largest priority to p.
    void updatePriority(int k, int newP) {
        int n = size();
        if (k < 1 || k > n) {
            throw std::out_of_range("updatePriority: k out of range");
        }
        if (newP < 1 || newP > maxP) {
            throw std::out_of_range("updatePriority: newP out of range");
        }
        if (presentPriority(root, 1, maxP, newP)) {
            throw std::logic_error("updatePriority: new priority already present");
        }

        // Erase old priority, insert new priority
        T v = erase(root, 1, maxP, k);
        insert(root, 1, maxP, newP, v);
        return;
    }

    // **API FUNCTION**
    // NEXTWITH(k, f)
    // returns the smallest j >= k such that f(QUERY(j)) == true,
    // or size() + 1 if no such j exists.
    int nextWith(int k, const std::function<bool(const T&)>& f) const {
        int n = size();
        if (n == 0) {
            return 1; // l + 1 where l = 0
        }

        int p = k;
        if (p < 1) p = 1;
        if (p > n) return n + 1;

        int i = 0;
        while (p <= n) {
            int len = 1 << i;         // 2^i
            int end = p + len - 1;
            if (end > n) end = n;

            // Parallel scan of QUERY(p..end)
            int best = nextWithRange(p, end, f);

            if (best <= end) {
                return best;  // found smallest j in this phase
            }

            p += len; // advance start by 2^i
            ++i;
        }

        return n + 1;
    }

    
    int nextWithRange(int L, int R, const std::function<bool(const T&)>& f) const {
        int n = size();
        if (n == 0) {
            return 1;
        }

        int best = n + 1;

        // Parallel loop over j in [L, R].
        #pragma omp parallel for reduction(min:best)
        for (int j = L; j <= R; ++j) {
            T val = query(j);
            if (f(val) && j < best) {
                best = j;
            }
        }

        return best;
    }


private:
    struct Node {
        int cnt;           // number of elements in this interval
        bool present;      // is there a value with this priority (meaningful for leaves)
        T value;           // value at node (if present)
        Node* left;        // left child
        Node* right;       // right child

        Node() : cnt(0), present(false), left(nullptr), right(nullptr) {}
    };

    int maxP;           // max priority
    Node* root;         // root

    // create node if null
    static void ensureNode(Node*& node) {
        if (!node) {
            node = new Node();
        }
    }

    // insert (v, p)
    // recurse over nodes "node" which span interval ["L", "R"]
    void insert(Node*& node, int L, int R, int p, const T& v) {
        ensureNode(node);
        node->cnt += 1;

        // base case -- reached leaf
        if (L == R) {
            node->present = true;
            node->value = v;
            return;
        }

        // recursive step
        int mid = (L + R) / 2;
        if (p <= mid) {
            insert(node->left, L, mid, p, v);
        } else {
            insert(node->right, mid+1, R, p, v);
        }
    }

    // erase element at kth priority (k-th largest), and return its value
    // recurse over nodes "node" which span interval ["L", "R"]
    T erase(Node*& node, int L, int R, int k) {

        node->cnt -= 1; // TODO: if we ever reach a node with count 0, we can delete the whole thing to save memory
                        // Pass down pointer to the node where cnt switched from >1 to =1

        // base case -- reached leaf
        if (L == R) {
            node->present = false;
            return node->value;
        }

        int mid = (L + R) / 2;
        int rightCount = (node->right ? node->right->cnt : 0);
        if (rightCount >= k) {
            // k-th largest is in right subtree
            return erase(node->right, mid + 1, R, k);
        } else {
            // k-th largest is in left subtree
            return erase(node->left, L, mid, k - rightCount);
        }
    }

    // is there an element with priority p?
    // recurse over nodes "node" which span interval ["L", "R"]
    bool presentPriority(Node* node, int L, int R, int p) const {

        // base cases
        if (!node) return false;
        if (L == R) return node->present;

        // recursive step
        int mid = (L + R) / 2;
        if (p <= mid) {
            return presentPriority(node->left, L, mid, p);
        }
        else {
            return presentPriority(node->right, mid + 1, R, p);
        }

        // TODO: could terminate early if count==0
    }


    // items: (value, priority) sorted by priority (.second)
    Node* buildFromSorted(const std::vector<std::pair<T,int>>& items,
                          int start, int end, // indices spanned by items
                          int L, int R,       // interval spanned by this node
                          int depth,
                          int maxParallelDepth) {
        
        if (start >= end) { // no elements
            return nullptr;
        }

        Node* node = new Node();
        node->cnt = end - start;  // number of elements in this subtree

        if (L == R) {
            // Leaf. All items[start..end) share priority L; under uniqueness, end-start == 1.
            node->present = true;
            node->value   = items[start].first;  // value (since pair is (value, priority))
            return node;
        }

        int mid = (L + R) / 2;

        // Split items[start..end) into left (priority <= mid) and right (> mid)
        auto it = std::lower_bound(
            items.begin() + start, items.begin() + end,
            mid + 1,
            [](const std::pair<T,int>& pr, int value) {
                return pr.second < value;  // compare priority with mid+1
            }
        );
        int m = static_cast<int>(it - items.begin()); // index of split

        Node* leftChild  = nullptr;
        Node* rightChild = nullptr;

        bool hasLeft  = (start < m);
        bool hasRight = (m < end);

        // Threshold to avoid spawning tasks for tiny subtrees
        const int THRESH = 32;

        bool canParallelizeHere = (depth < maxParallelDepth) &&
                                  (end - start >= THRESH) &&
                                  hasLeft && hasRight;

        if (canParallelizeHere) {
            // Spawn ONE subtree as a task; current thread handles the other.
            #pragma omp task shared(leftChild)
            {
                leftChild = buildFromSorted(items, start, m, L, mid,
                                            depth + 1, maxParallelDepth);
            }
            rightChild = buildFromSorted(items, m, end, mid + 1, R,
                                         depth + 1, maxParallelDepth);
            #pragma omp taskwait
        } else {
            // recurse linearly (no new tasks)
            if (hasLeft) {
                leftChild = buildFromSorted(items, start, m, L, mid,
                                            depth + 1, maxParallelDepth);
            }
            if (hasRight) {
                rightChild = buildFromSorted(items, m, end, mid + 1, R,
                                             depth + 1, maxParallelDepth);
            }
        }

        node->left  = leftChild;
        node->right = rightChild;
        return node;
    }



    // return the value with k-th largest priority
    T queryByRank(Node* node, int L, int R, int k) const {
        if (!node || k < 1 || k > node->cnt) {
            throw std::logic_error("queryByRank: inconsistent tree");
        }

        if (L == R) {
            // leaf
            return node->value;
        }
        int mid = (L + R) / 2;
        int rightCount = (node->right ? node->right->cnt : 0);
        if (rightCount >= k) {
            // k-th largest is in right subtree
            return queryByRank(node->right, mid + 1, R, k);
        } else {
            // k-th largest is in left subtree
            return queryByRank(node->left, L, mid, k - rightCount);
        }
    }

    // helper for UPDATEVALUE: update the value of k-th largest element to v
    void updateValueHelper(Node* node, int L, int R, int k, const T& v) {
        if (!node || k < 1 || k > node->cnt) {
            throw std::logic_error("updateValueHelper: inconsistent tree");
        }

        if (L == R) {
            // leaf
            node->value = v;
            return;
        }
        int mid = (L + R) / 2;
        int rightCount = (node->right ? node->right->cnt : 0);
        if (rightCount >= k) {
            // k-th largest is in right subtree
            updateValueHelper(node->right, mid+1, R, k, v);
            return;
        } else {
            // k-th largest is in left subtree
            updateValueHelper(node->left, L, mid, k - rightCount, v);
            return;
        }
    }

    // helper for FIND(p)
    // recurse over nodes "node" which span interval ["L", "R"]
    // rank = how many elements have priority > p so far
    std::pair<T,int> findByPriority(Node* node, int L, int R, int p, int rank) const {
        if (!node || node->cnt == 0) {
            // ERROR

            throw std::logic_error("findByPriority: priority not present");
        }

        if (L == R) {
            if (node->cnt == 0 || !node->present) {
                // ERROR
                throw std::logic_error("findByPriority: priority not present at leaf");
            }
            return {node->value, rank + 1};
        }

        int mid = (L + R) / 2;
        if (p <= mid) {
            // p in left subtree
            int rightCount = (node->right ? node->right->cnt : 0);
            return findByPriority(node->left, L, mid, p, rank + rightCount);
        } else {
            // p in right subtree
            return findByPriority(node->right, mid + 1, R, p, rank);
        }
    }
};

int main() {
    int maxP = 1000;
    PriorityStructure<int> ps(maxP);

    std::vector<std::pair<int,int>> elems;
    elems.push_back({100, 10});   // value=100, priority=10
    elems.push_back({200, 150});
    elems.push_back({300, 999});
    elems.push_back({400, 500});
    elems.push_back({500, 1});
    elems.push_back({600, 750});
    elems.push_back({700, 250});
    elems.push_back({800, 900});
    elems.push_back({900, 333});
    elems.push_back({1000, 42});
    elems.push_back({1100, 600});
    elems.push_back({1200, 700});
    elems.push_back({1300, 800});
    elems.push_back({1400, 5});
    elems.push_back({1500, 444});
    elems.push_back({1600, 222});
    elems.push_back({1700, 321});
    elems.push_back({1800, 888});
    elems.push_back({1900, 50});
    elems.push_back({2000, 430});

    ps.initialize(elems);

    std::cout << "Size after initialize: " << ps.size() << "\n\n";

    // Print elements in order of rank
    std::cout << "By rank (k-th largest priority):\n";
    int n = ps.size();
    for (int k = 1; k <= n; ++k) {
        int v = ps.query(k);
        std::cout << "  k=" << k << " -> value=" << v << "\n";
    }

    // Print value and rank returned by find(p)
    std::cout << "\nBy explicit priority (find):\n";
    for (const auto& [val, p] : elems) {
        auto [v, rank] = ps.find(p);
        std::cout << "  priority=" << p
                  << " -> value=" << v
                  << ", rank=" << rank << "\n";
    }

    return 0;
}
