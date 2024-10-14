//
// Created by victo on 13/10/2024.
//

#ifndef LAB01_NONCOOPERATIVEMULTITHREADING_VARIABLESYSTEM_HPP
#define LAB01_NONCOOPERATIVEMULTITHREADING_VARIABLESYSTEM_HPP

#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

class VariableSystem {
private:
    const size_t size;
    std::vector<int> variables;
    const std::vector<std::vector<size_t>> dependencies;
    const std::vector<std::vector<size_t>> dependents;
    std::vector<std::unique_ptr<std::mutex>> locks;
    std::vector<std::thread> threads;

    static constexpr int WORKER_ITER_COUNT = 100;
    static constexpr int CC_ITER_COUNT = 40;
    static constexpr int WORKER_MAX_SLEEP_TIME_MS = 10;
    static constexpr int WORKER_THREAD_MIN_INITIAL_SLEEP_MS = 100;
    static constexpr int CC_MAX_SLEEP_TIME_MS = 50;
    static constexpr int UPDATE_VALUE_SPREAD = 20;
    static constexpr int UPDATE_VALUE_MEAN = 10;

    [[nodiscard]] static auto random() -> int;

    [[nodiscard]] static auto
    search(size_t startID, const std::vector<std::vector<size_t>> &searchSpace) -> std::vector<size_t>;

    [[nodiscard]] auto variablesAsString() const -> std::string;

    [[nodiscard]] auto computeDependents() const -> std::vector<std::vector<size_t>>;

    [[nodiscard]] auto createLocks() const -> std::vector<std::unique_ptr<std::mutex>>;

    [[nodiscard]] auto createVariables() const -> std::vector<int>;

    [[nodiscard]] auto getAllDependents(size_t variableID) const -> std::set<size_t>;

    void updateVariable(size_t variableId, int delta);

    void checkConsistency() const;

    void startThreads();

    void gatherThreads();

public:
    explicit VariableSystem(const std::vector<std::vector<size_t>> &&deps);
};

#endif //LAB01_NONCOOPERATIVEMULTITHREADING_VARIABLESYSTEM_HPP
