#include <vector>
#include <functional>
#include <stdexcept>
#include <iostream>
#include <string>

template <typename T>
class PriorityStructure {
public:
    explicit PriorityStructure(int maxPriority)
        : maxP(maxPriority), root(nullptr) {}

    // **API FUNCTION**
    // INITIALIZE({(v1, p1), ..., (vl, pl)})
    // initialize segment tree from list of (value, priority) pairs
    void initialize(const std::vector<std::pair<T,int>>& elems) {
    root = nullptr;

    int m = elems.size();

    // Parallelize over the elements: each iteration corresponds to one (v,p).
    #pragma omp parallel for
    for (int i = 0; i < m; ++i) {
        const T& v = elems[i].first;
        int p = elems[i].second;

        // Local validation (doesn't touch shared state)
        if (p < 1 || p > maxP) {
            // Throwing exceptions from inside OpenMP regions is messy in practice,
            // but this is fine as a "dummy" example.
            throw std::out_of_range("priority out of range in initialize");
        }

        // Now we need to touch the shared tree (root and its descendants).
        // We *must* serialize this to avoid races on node creation and cnt updates.
        #pragma omp critical(PriorityTreeInsert)
        {
            if (presentPriority(root, 1, maxP, p)) { // priorities must be unique
                throw std::logic_error("duplicate priority in initialize");
            }
            insert(root, 1, maxP, p, v);
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
            int best = nextWithRange(p, end, f)

            if (best <= end) {
                return best;  // found smallest j in this phase
            }

            p += len; // advance start by 2^i
            ++i;
        }

        return n + 1;
    }

    
    int nextWithRange(int min, int max, const std::function<bool(const T&)>& f) const {
        int n = size();
        if (n == 0) {
            return 1;
        }

        // Clamp range
        if (max > n)  max = n;
        if (min > max) {
            return n + 1;
        }

        int best = n + 1;

        // Parallel loop over j in [L, R].
        #pragma omp parallel for reduction(min:best)
        for (int j = L; j <= R; ++j) {
            const T& val = query(j);
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





// parallelize methods like erase which mutate the tree?
// array based tree which takes longer to initialize but easier to parallelize
// how to parallelize this