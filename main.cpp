#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>
#include <mutex>
#include <memory>
#include <numeric>
#include <set>
#include <string>
#include <syncstream>
#include <thread>
#include <vector>

// 3. Summation with fixed structure of inputs
//
// We have to keep the values of some integer variables.
// Some of them are primary variables; they represent input data.
// The others are secondary variables, and represent aggregations of some other variables.
// In our case, each secondary variable is a sum of some input variables.
// The inputs may be primary or secondary variables.
// However, we assume that the relations do not form cycles.
//
// At runtime, we get notifications of value changes for the primary variable.
// Processing a notification must atomically update the primary variable,
// as well as any secondary variable depending, directly or indirectly, on it.
// The updating shall not re-compute the sums;
// instead, you must use the difference between the old value and the new value of the primary variable.
//
// From time to time, as well as at the end, a consistency check shall be performed.
// It shall verify that all the secondary variables are indeed the sums of their inputs, as specified.
//
//  Two updates involving distinct variables must be able to proceed independently
//  (without having to wait for the same mutex).

class VariableSystem {
private:
    const size_t size;
    std::vector<int> variables;
    const std::vector<std::vector<size_t>> dependencies;
    const std::vector<std::vector<size_t>> dependents;
    std::vector<std::unique_ptr<std::mutex>> locks;
    std::vector<std::thread> threads;
    static constexpr int WORKER_ITER_COUNT = 20;
    static constexpr int WORKER_MAX_SLEEP_TIME_MS = 5000;
    static constexpr int CC_ITER_COUNT = 30;
    static constexpr int CC_MAX_SLEEP_TIME_MS = 1000;
    static constexpr int UPDATE_RANGE = 20;
    static constexpr int UPDATE_MEAN_VALUE = 10;

    [[nodiscard]] std::string variablesAsString() const {
        std::ostringstream oss;
        oss << "[";
        for (auto i = 0; i < variables.size(); ++i) {
            oss << "{" << i << " : " << variables[i] << "}";
            if (i < variables.size() - 1) {
                oss << ", ";
            }
        }
        oss << "]";

        return oss.str();
    }

    std::vector<std::vector<size_t>> computeDependents() {
        std::vector<std::vector<size_t>> inverseDependencies{};
        inverseDependencies.reserve(size);
        for (auto dependentIndex = 0; dependentIndex < size; ++dependentIndex) {
            inverseDependencies.emplace_back();
            for (auto dependencyIndex = 0; dependencyIndex < size; ++dependencyIndex) {
                if (std::find(dependencies[dependencyIndex].begin(),
                              dependencies[dependencyIndex].end(),
                              dependentIndex) != dependencies[dependencyIndex].cend()) {
                    inverseDependencies[dependentIndex].emplace_back(dependencyIndex);
                }
            }
        }
        return inverseDependencies;
    }

    [[nodiscard]] std::vector<std::unique_ptr<std::mutex>> createLocks() const {
        std::vector<std::unique_ptr<std::mutex>> mutexVector;
        mutexVector.reserve(size);
        for (auto i = 0; i < size; ++i) { mutexVector.emplace_back(std::make_unique<std::mutex>()); }
        return mutexVector;
    }

    [[nodiscard]] std::vector<int> createVariables() const {
        std::vector<int> variableVector;
        variableVector.resize(size, 0);
        return variableVector;
    }

    static std::vector<size_t> search(size_t startID, const std::vector<std::vector<size_t>> &searchSpace) {
        std::set<size_t> visited{startID};
        std::vector<size_t> result{startID};
        // Impure function that modifies the 'visited' and 'result' objects
        std::function<void(size_t)> dfs = [&](int var) {
            for (auto dep: searchSpace[var]) {
                result.push_back(dep);
                if (visited.find(dep) == visited.end()) {
                    visited.insert(dep);
                    dfs(dep);
                }
            }
        };
        dfs(startID);
        std::sort(result.begin(), result.end());
        return result;
    }

    static std::vector<size_t> uniqueSearch(size_t startID, const std::vector<std::vector<size_t>> &searchSpace) {
        std::set<size_t> visited{startID};
        std::vector<size_t> result{startID};
        // Impure function that modifies the 'visited' and 'result' objects
        std::function<void(size_t)> dfs = [&](int var) {
            for (const auto dep: searchSpace[var]) {
                if (visited.find(dep) == visited.end()) {
                    visited.insert(dep);
                    dfs(dep);
                    result.push_back(dep);
                }
            }
        };
        dfs(startID);
        std::sort(result.begin(), result.end());
        return result;
    }

    // Traverse the dependents tree and gather all direct and indirect dependencies of the variable with said id
    // The return vector is sorted in order to avoid deadlocks
    // Does not need to be synchronised as of now, since the dependency matrix is constant across the program's runtime
    [[nodiscard]] std::vector<size_t> getAllDependents(size_t variableID) const {
        assert(variableID < size && "Trying to read dependents of a variable that is not part of the system");
        return uniqueSearch(variableID, dependents);
    }

    void updateVariable(size_t variableId, int delta) {
        assert(variableId < size && "Trying to update a variable that is not part of the system");
        assert(dependencies[variableId].empty() && "Trying to update a non-primary variable");
        std::osyncstream(std::cout) << "[Thread " << std::this_thread::get_id() << "] Update " << variableId << " by "
                                    << delta
                                    << std::endl;
        const auto variablesDependents = getAllDependents(variableId);
        // for all variables that depend on our current variable, gather their locks and use a lock guard to
        // lock them all at once and update their values
        std::vector<std::unique_lock<std::mutex>> lockGuards;
        lockGuards.reserve(variablesDependents.size());
        for (const auto dep: variablesDependents) {
            lockGuards.emplace_back(*locks[dep]);
        }
        for (const auto id: search(variableId, dependents)) {
            variables[id] += delta;
        }
    }

    void checkConsistency() const {
        std::vector<std::unique_lock<std::mutex>> lockGuards;
        lockGuards.reserve(variables.size());
        for (const auto &lock: locks) {
            lockGuards.emplace_back(*lock);
        }
        std::osyncstream(std::cout) << "[CC] Starting" << std::endl;
        for (int index = 0; index < size; ++index) {
            if (dependencies[index].empty()) { continue; }
            const auto expectedValue = std::accumulate(dependencies[index].cbegin(),
                                                       dependencies[index].cend(),
                                                       0,
                                                       [&](int partialSum, int valueId) {
                                                           return partialSum + variables[valueId];
                                                       });
            const auto actualValue = variables[index];

            assert(expectedValue == actualValue && "[CC] Failure");
        }
        std::osyncstream(std::cout) << "[CC] Success:\n" << variablesAsString() << std::endl;
    }


#pragma clang diagnostic push
#pragma ide diagnostic ignored "cert-msc50-cpp"

    void startThreads() {
        const auto baseVariableCount = std::count_if(dependencies.cbegin(),
                                                     dependencies.cend(),
                                                     [](const auto &dependencyVector) { return dependencyVector.empty(); });
        const auto workerThreadBody = [this]() {
            std::osyncstream(std::cout) << "[Thread " << std::this_thread::get_id() << "] Start" << std::endl;
            std::srand(std::chrono::system_clock::now().time_since_epoch().count());
            int i = 0;
            while (++i < WORKER_ITER_COUNT) {
                const auto variableId = std::rand() % size;
                if (dependencies[variableId].empty()) {
                    updateVariable(variableId, std::rand() % UPDATE_RANGE - UPDATE_MEAN_VALUE);
                    std::this_thread::sleep_for(std::chrono::milliseconds(std::rand() % WORKER_MAX_SLEEP_TIME_MS));
                }
            }
            std::osyncstream(std::cout) << "[Thread " << std::this_thread::get_id() << "] End" << std::endl;
        };
        const auto ccThreadBody =
                [this]() {
                    std::srand(std::chrono::system_clock::now().time_since_epoch().count());
                    int i = 0;
                    while (++i < CC_ITER_COUNT) {
                        checkConsistency();
                        std::this_thread::sleep_for(std::chrono::milliseconds(std::rand() % CC_MAX_SLEEP_TIME_MS));
                    }
                };
        std::osyncstream(std::cout) << "[Main] Starting worker threads" << std::endl;
        threads.reserve(baseVariableCount + 1);
        for (int index = 0; index < baseVariableCount; ++index) {
            threads.emplace_back(workerThreadBody);
        }
        threads.emplace_back(ccThreadBody);
    }

#pragma clang diagnostic pop

    void gatherThreads() {
        std::osyncstream(std::cout) << "[Main] waiting for workers" << std::endl;
        for (auto &thread: threads) {
            if (thread.joinable()) { thread.join(); }
        }
        std::osyncstream(std::cout) << "[Main] gathered threads" << std::endl;
        checkConsistency();
    }

public:
    explicit VariableSystem(const std::vector<std::vector<size_t>> &&deps) : dependencies(deps),
                                                                             dependents(computeDependents()),
                                                                             locks(createLocks()),
                                                                             variables(createVariables()),
                                                                             size(deps.size()) {
        assert(size == variables.size() && "Mismatch between variable vector size and system size");
        assert(size == dependencies.size() && "Mismatch between dependencies vector size and system size");
        assert(size == dependents.size() && "Mismatch between dependents vector size and system size");
        assert(size == locks.size() && "Mismatch between locks vector size and system size");
        startThreads();
        gatherThreads();
    }
};

int main() {
    VariableSystem system({
                                  {}          /*  0 */,
                                  {}          /*  1 */,
                                  {}          /*  2 */,
                                  {}          /*  3 */,
                                  {2, 1}      /*  4 */,
                                  {1, 2}      /*  5 */,
                                  {4, 3, 0}   /*  6 */,
                                  {}          /*  7 */,
                                  {7, 6}      /*  8 */,
                                  {2}         /*  9 */,
                                  {8, 9, 5}   /* 10 */
                          });
    return 0;
}
