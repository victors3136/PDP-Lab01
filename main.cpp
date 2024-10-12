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
        std::vector<std::unique_ptr<std::mutex>> lox;
        lox.reserve(dependencies.size());
        for (auto i = 0; i < dependencies.size(); ++i) { lox.emplace_back(std::make_unique<std::mutex>()); }
        return lox;
    }

    static auto search(int startID, const std::vector<std::vector<int>> &searchSpace) {
        std::set<int> visited{startID};
        std::vector<int> result{startID};
        // Impure function that modifies the 'visited' and 'result' objects
        std::function<void(int)> dfs = [&](int var) {
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

public:
    explicit SensorSystem(const std::vector<std::vector<int>> &&deps) : dependencies(deps),
                                                                        dependents(computeDependents()),
                                                                        locks(createLocks()),
                                                                        size(deps.size()) {
//        std::cout << "Size              " << size << std::endl;
//        std::cout << "Dependencies Size " << dependencies.size() << std::endl;
//        std::cout << "Dependents Size   " << dependents.size() << std::endl;
//        std::cout << "Locks Size        " << locks.size() << std::endl;
        assert(size == dependencies.size());
        assert(size == dependents.size());
        assert(size == locks.size());
        variables.resize(size, 0);
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
    auto getAllDependents(int variableID) {
        assert(variableID < size);
        return search(variableID, dependents);
    }

    // Now we need a function that spawns threads. There should be about as many threads as input variables
    // (variables with no dependencies)
    // The thread should execute a function that should be a member variable of the class
    auto spawnThreads() {
        static auto baseSensorsCount = std::count_if(dependencies.cbegin(),
                                                     dependencies.cend(),
                                                     [](const auto &dependencyVector) { return dependencyVector.empty(); });
        threads.reserve(baseSensorsCount + 1);
        for (int index = 0; index < baseSensorsCount; ++index) {
            threads.emplace_back([index]() {
                std::srand(index);
                const auto sleepTime = std::rand() % 10; // NOLINT(*-msc50-cpp)
                std::this_thread::sleep_for(std::chrono::seconds(sleepTime));
                std::osyncstream(std::cout) << "Thread " << std::this_thread::get_id() << " slept for " << sleepTime
                                            << " seconds" << std::endl;
            } /* Run updates on a base sensor*/);
        }
        threads.emplace_back([]() {}/*Consistency checker*/);
    }

    auto gatherThreads() {
        for (auto &thread: threads) {
            if (thread.joinable()) { thread.join(); }
        }
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
    system.spawnThreads();
    system.gatherThreads();
    return 0;
}
