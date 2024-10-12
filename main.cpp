#include <algorithm>
#include <cassert>
#include <mutex>
#include <iostream>
#include <variant>
#include <vector>
#include <memory>
#include <set>
#include <functional>
#include <thread>
#include <syncstream>
#include <numeric>
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

class SensorSystem {
private:
    const size_t size;
    std::vector<int> variables;
    const std::vector<std::vector<int>> dependencies;
    const std::vector<std::vector<int>> dependents;
    std::vector<std::unique_ptr<std::mutex>> locks;
    std::vector<std::thread> threads;
    std::osyncstream logger;

    auto computeDependents() {
        std::vector<std::vector<int>> inverseDependencies{};
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

    auto createLocks() {
        std::vector<std::unique_ptr<std::mutex>> mutexVector;
        mutexVector.reserve(dependencies.size());
        for (auto i = 0; i < dependencies.size(); ++i) { mutexVector.emplace_back(std::make_unique<std::mutex>()); }
        return mutexVector;
    }

    static auto search(size_t startID, const std::vector<std::vector<int>> &searchSpace) {
        std::set<size_t> visited{startID};
        std::vector<size_t> result{startID};
        // Impure function that modifies the 'visited' and 'result' objects
        std::function<void(size_t)> dfs = [&](int var) {
            for (int dep: searchSpace[var]) {
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


    // Traverse the dependency tree and gather all direct and indirect dependencies of the variable with said id
    // The return vector is sorted in order to avoid deadlocks
    // Does not need to be synchronised as of now, since the dependency matrix is constant across the program's runtime
    auto getAllDependencies(int variableID) {
        assert(variableID < size);
        return search(variableID, dependencies);
    }

    // Traverse the dependents tree and gather all direct and indirect dependencies of the variable with said id
    // The return vector is sorted in order to avoid deadlocks
    // Does not need to be synchronised as of now, since the dependency matrix is constant across the program's runtime
    auto getAllDependents(size_t variableID) {
        assert(variableID < size);
        return search(variableID, dependents);
    }

    auto updateVariable(size_t variableID, int delta) {
        assert(variableID < size && "Trying to update a variable that is not part of the system");
        assert(dependencies[variableID].empty() && "Trying to update a non-primary variable");
        std::cout << "[Thread " << std::this_thread::get_id() << "] Update " << variableID << " by " << delta
                  << std::endl;
        const auto variablesDependents = getAllDependents(variableID);
        // for all variables that depend on our current variable, gather their locks and use a lock guard to
        // lock them all at once and update their values
        std::vector<std::unique_lock<std::mutex>> lockGuards;
        lockGuards.reserve(variablesDependents.size());
        for (const auto dep: variablesDependents) {
            lockGuards.emplace_back(*locks[dep]);
        }

    }

    auto checkConsistency() {
        std::vector<std::unique_lock<std::mutex>> lockGuards;
        lockGuards.reserve(variables.size());
        for (int index = 0; index < size; ++index) {
            lockGuards.emplace_back(*locks[index]);
        }
        std::cout << "[CC] Starting" << std::endl;
        for (int index = 0; index < size; ++index) {
            const auto expectedValue = std::accumulate(dependencies[index].cbegin(),
                                                       dependencies[index].cend(),
                                                       0,
                                                       [&](int partialSum, int valueId) {
                                                           return partialSum + variables[valueId];
                                                       });
            const auto actualValue = variables[index];
            assert(expectedValue == actualValue);
        }
        std::cout << "[CC] Success" << std::endl;
    }

public:
    explicit SensorSystem(const std::vector<std::vector<int>> &&deps) : dependencies(deps),
                                                                        dependents(computeDependents()),
                                                                        locks(createLocks()),
                                                                        size(deps.size()),
                                                                        logger(std::cout) {
//        std::cout << "Size              " << size << std::endl;
//        std::cout << "Dependencies Size " << dependencies.size() << std::endl;
//        std::cout << "Dependents Size   " << dependents.size() << std::endl;
//        std::cout << "Locks Size        " << locks.size() << std::endl;
        assert(size == dependencies.size());
        assert(size == dependents.size());
        assert(size == locks.size());
        variables.resize(size, 0);
    }

#pragma clang diagnostic push
#pragma ide diagnostic ignored "cert-msc50-cpp"

    // Now we need a function that spawns threads. There should be about as many threads as input variables
    // (variables with no dependencies)
    // The thread should execute a function that should be a member variable of the class
    auto startThreads() {
        static auto baseSensorsCount = std::count_if(dependencies.cbegin(),
                                                     dependencies.cend(),
                                                     [](const auto &dependencyVector) { return dependencyVector.empty(); });
        std::cout << "[Main] Starting worker threads" << std::endl;
        threads.reserve(baseSensorsCount + 1);
        for (int index = 0; index < baseSensorsCount; ++index) {
            threads.emplace_back([this]() {
                std::cout << "[Thread " << std::this_thread::get_id() << "] start" << std::endl;
                std::srand(std::chrono::system_clock::now().time_since_epoch().count());
                int i = 0;
                while (++i < 20) {
                    const auto variableId = std::rand() % size;
                    if (dependencies[variableId].empty()) {
                        updateVariable(variableId, std::rand() % 20 - 10);
                        // sleep for at most 5 seconds
                        std::this_thread::sleep_for(std::chrono::milliseconds(std::rand() % 5000));
                    }
                }
                std::cout << "[Thread " << std::this_thread::get_id() << "] end" << std::endl;
            });
        }
        threads.emplace_back(/*Consistency checker*/
                [this]() {
                    std::srand(std::chrono::system_clock::now().time_since_epoch().count());
                    int i = 0;
                    while (++i < 10) {
                        checkConsistency();
                        std::this_thread::sleep_for(std::chrono::milliseconds(std::rand() % 1000));
                    }
                });
    }

#pragma clang diagnostic pop

    auto gatherThreads() {
        std::cout << "[Main] waiting for workers" << std::endl;
        for (auto &thread: threads) {
            if (thread.joinable()) { thread.join(); }
        }
        std::cout << "[Main] gathered threads" << std::endl;
        checkConsistency();
    }

};

int main() {
    std::vector<std::vector<int>> dependencies = {
            {},
            {},
            {},
            {},
            {},
            {1, 2},
            {4, 3, 0},
            {},
            {7, 6},
            {2},
            {8, 9, 5}
    };
    SensorSystem system(std::move(dependencies));
    system.startThreads();
    system.gatherThreads();
    return 0;
}
