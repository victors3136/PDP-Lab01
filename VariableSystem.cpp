//
// Created by victo on 13/10/2024.
//

#include "VariableSystem.hpp"

#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>
#include <latch>
#include <numeric>
#include <random>
#include <stack>
#include <string>
#include <syncstream>
#include <thread>

auto VariableSystem::random() -> int {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<int> dist(0, INT_MAX);
    return dist(gen);
}

auto
VariableSystem::search(const size_t startID, const std::vector<std::vector<size_t>> &searchSpace) -> std::vector<size_t> {
    std::set<size_t> visited{startID};
    std::vector<size_t> result{startID};
    std::stack<size_t> stack;
    stack.emplace(startID);
    while (!stack.empty()) {
        const auto current = stack.top();
        stack.pop();
        for (const auto dep: searchSpace[current]) {
            result.emplace_back(dep);
            if (visited.find(dep) == visited.end()) {
                visited.insert(dep);
                stack.emplace(dep);
            }
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

VariableSystem::VariableSystem(const std::vector<std::vector<size_t>> &&deps)
        : dependencies(deps),
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

auto VariableSystem::variablesAsString() const -> std::string {
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

auto VariableSystem::computeDependents() const -> std::vector<std::vector<size_t>> {
    std::vector<std::vector<size_t>> inverseDependencies{};
    inverseDependencies.reserve(size);
    for (auto dependentIndex = 0; dependentIndex < size; ++dependentIndex) {
        inverseDependencies.emplace_back();
        for (auto dependencyIndex = 0; dependencyIndex < size; ++dependencyIndex) {
            const auto count = std::count(dependencies[dependencyIndex].cbegin(),
                                          dependencies[dependencyIndex].cend(),
                                          dependentIndex);
            for (int i = 0; i < count; ++i) {
                inverseDependencies[dependentIndex].emplace_back(dependencyIndex);
            }
        }
    }
    return inverseDependencies;
}

auto VariableSystem::createLocks() const -> std::vector<std::unique_ptr<std::mutex>> {
    std::vector<std::unique_ptr<std::mutex>> mutexVector;
    mutexVector.reserve(size);
    for (auto i = 0; i < size; ++i) {
        mutexVector.emplace_back(std::make_unique<std::mutex>());
    }
    return mutexVector;
}

auto VariableSystem::createVariables() const -> std::vector<int> {
    std::vector<int> variableVector;
    variableVector.resize(size, 0);
    return variableVector;
}

auto VariableSystem::getAllDependents(size_t variableID) const -> std::set<size_t> {
    assert(variableID < size && "Trying to read dependents of a variable that is not part of the system");
    const auto vector = search(variableID, dependents);
    return {vector.cbegin(), vector.cend()};
}

void VariableSystem::updateVariable(size_t variableId, int delta) { // NOLINT(*-easily-swappable-parameters)
    assert(variableId < size && "Trying to update a variable that is not part of the system");
    assert(dependencies[variableId].empty() && "Trying to update a non-primary variable");
    const auto variablesDependents = getAllDependents(variableId);
    std::vector<std::unique_lock<std::mutex>> lockGuards;
    lockGuards.reserve(variablesDependents.size());
    for (const auto dep: variablesDependents) {
        lockGuards.emplace_back(*locks[dep]);
    }
    for (const auto id: search(variableId, dependents)) {
        variables[id] += delta;
        std::osyncstream(std::cout) << "[Thread " << std::this_thread::get_id() << "] Update " << id << " by "
                                    << delta << '\n';
    }
}

void VariableSystem::checkConsistency() const {
    std::vector<std::unique_lock<std::mutex>> lockGuards;
    lockGuards.reserve(variables.size());
    for (const auto &lock: locks) {
        lockGuards.emplace_back(*lock);
    }
    std::osyncstream(std::cout) << "[CC] Starting\n";
    for (int index = 0; index < size; ++index) {
        if (dependencies[index].empty()) { continue; }
        const auto expectedValue = std::accumulate(dependencies[index].cbegin(),
                                                   dependencies[index].cend(),
                                                   0,
                                                   [&](int partialSum, int valueId) {
                                                       return partialSum + variables[valueId];
                                                   });
        const auto actualValue = variables[index];
        if (expectedValue != actualValue) {
            std::osyncstream(std::cout) << "[CC] Failure when checking consistency for variable " << index << ":\n"
                                        << "Expected: " << expectedValue << " but got " << actualValue << '\n'
                                        << variablesAsString() << '\n';
            assert(false);
        }
    }
    std::osyncstream(std::cout) << "[CC] Success:\n" << variablesAsString() << '\n';
}

void VariableSystem::startThreads() {
    const auto baseVariableCount = std::count_if(dependencies.cbegin(),
                                                 dependencies.cend(),
                                                 [](const auto &dependencyVector) { return dependencyVector.empty(); });
    const auto workerThreadBody = [this]() {
        std::osyncstream(std::cout) << "[Thread " << std::this_thread::get_id()
                                    << "] About to take a nap\n";
        std::this_thread::sleep_for(
                std::chrono::milliseconds(random() % WORKER_MAX_SLEEP_TIME_MS) +
                std::chrono::milliseconds(WORKER_THREAD_MIN_INITIAL_SLEEP));
        for (auto i = 0; i < WORKER_ITER_COUNT; ++i) {
            const auto variableId = random() % size;
            if (!dependencies[variableId].empty()) {
                --i /*stall 1 iteration*/;
                continue;
            }
            const auto delta = random() % UPDATE_RANGE - UPDATE_MEAN_VALUE;
            if (!delta) {
                --i /*stall 1 iteration*/;
                continue;
            }
            updateVariable(variableId, delta);
            std::this_thread::sleep_for(std::chrono::milliseconds(random() % WORKER_MAX_SLEEP_TIME_MS));
        }
        std::osyncstream(std::cout) << "[Thread " << std::this_thread::get_id() << "] End\n";
    };
    const auto ccThreadBody = [this]() {
        std::osyncstream(std::cout) << "[CC Thread " << std::this_thread::get_id() << "] About to take a nap\n";
        for (auto i = 0; i < CC_ITER_COUNT; ++i) {
            checkConsistency();
            std::this_thread::sleep_for(std::chrono::milliseconds(random() % CC_MAX_SLEEP_TIME_MS));
        }
        std::osyncstream(std::cout) << "[CC Thread " << std::this_thread::get_id() << "] Ended\n";
    };
    std::osyncstream(std::cout) << "[Main] Starting worker threads\n";
    threads.reserve(baseVariableCount + 1);
    for (int index = 0; index < baseVariableCount; ++index) {
        threads.emplace_back(workerThreadBody);
    }
    threads.emplace_back(ccThreadBody);
}

void VariableSystem::gatherThreads() {
    std::osyncstream(std::cout) << "[Main] waiting for workers\n";
    for (auto &thread: threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    std::osyncstream(std::cout) << "[Main] gathered threads\n";
    checkConsistency();
}
