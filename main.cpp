#include <algorithm>
#include <cassert>
#include <iostream>
#include <latch>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <vector>
#include <variant>

// 3. Summation with fixed structure of inputs
//
// We have to keep the values of some integer variables.
// Some of them are primary variables; they represent input data.
// The others are secondary variables, and represent aggregations of some other variables.
// In our case, each secondary variable is a sum of some input variables.
// The inputs may be primary or secondary variables.
// However, we assume that the relations do not form cycles.
//
// At runtime, we getValue notifications of value changes for the primary variable.
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
class Variable;

using VariableVector = std::vector<std::shared_ptr<Variable>>;

class Variable {
private:
    mutable std::mutex guard{};
    std::variant<int, VariableVector> input;

    auto value() const -> int {
        return std::get<int>(input);
    }

    auto sum() const -> int {
        static constexpr auto sum =
                [](auto sum, const auto &variable) -> int {
                    return sum + variable->getValue();
                };
        const auto &inputs = std::get<VariableVector>(input);

        std::vector<std::unique_lock<std::mutex>> locks;
        locks.reserve(inputs.size());
        for (const auto &variable: inputs) {
            locks.emplace_back(variable->guard);
        }
        return std::accumulate(inputs.begin(), inputs.end(), 0, sum);
    }

public:
    Variable() : input(0) {}

    Variable(const Variable &other) {
        std::lock_guard<std::mutex> lock(other.guard);
        input = other.input;
    }

    Variable(Variable &&other) noexcept {
        std::lock_guard<std::mutex> lock(other.guard);
        input = std::move(other.input);
    }

    Variable &operator=(const Variable &other) {
        if (this != &other) {
            std::lock_guard<std::mutex> lockThis(guard);
            std::lock_guard<std::mutex> lockOther(other.guard);
            input = other.input;
        }
        return *this;
    }


    Variable &operator=(Variable &&other) noexcept {
        if (this != &other) {
            std::lock_guard<std::mutex> lockThis(guard);
            std::lock_guard<std::mutex> lockOther(other.guard);
            input = std::move(other.input);
        }
        return *this;
    }

    // Construct a Variable from an int
    explicit Variable(int value = 0) : input(value) {}

    // Construct a Variable from a vector of other Variables
    explicit Variable(const VariableVector &values) : input(values) {}

    // Check if the Variable is a primitive
    inline auto primitive() const -> bool {
        return std::holds_alternative<int>(input);
    }

    // Get the value of a variable. This can happen twofold:
    //  - The input is just an int, in that case just read it
    //  - The input is a vec of other Variables. In this case, lock all variables and read their values,
    //  before summing them
    inline auto getValue() const -> int {
        return primitive()
               ? value()
               : sum();
    }

    // Method for setting the input to a certain value,
    // which should assume that the input is an int,
    // not a vector
    auto setValue(int newValue) -> void {
        std::lock_guard<std::mutex> lock(guard);
        assert(primitive() && "Value should only be set for primitive Variables");
        input = newValue;
    }

    inline std::mutex &getLock() const noexcept {
        return guard;
    }
};

class Solution {
private:
    static constexpr size_t Count = 10;

    Solution() {
        variables.reserve(Count);
        threads.reserve(Count);
    }

    std::vector<Variable> variables;
    std::vector<std::thread> threads;
public:
    static auto &Instantiate() {
        static Solution instance{};
        return instance;
    }

    // Create an array of Count simple variables and some complex ones
    auto setup() -> void {
        for (int num = 0; num < Count / 2; ++num) {
            variables.emplace_back(num);
        }
        for (int num = Count / 2; num < Count; ++num) {
            VariableVector inputs = {
                    std::make_shared<Variable>(variables[num - Count / 2]),
                    std::make_shared<Variable>(variables[(num + 1) - Count / 2])
            };
            variables.emplace_back(inputs);
        }
    }

    // Spawn Count threads which all tick one of the simple variables
    // and 1 thread that does consistency checks every once in a while
    auto run() -> void {
        std::latch startLine{Count + 1};
        std::latch finishLine{Count + 1};
        for (size_t currentId = 0; currentId < Count; ++currentId) {
            threads.emplace_back([this, currentId, &startLine, &finishLine]() -> void {
                startLine.count_down();
                std::cout << "Thread " << currentId << " started\n";
                for (auto _ = 0; _ < Solution::Count * 16; ++_) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    tick();
                }
                std::cout << "Thread " << currentId << " ended\n";
                finishLine.count_down();
            });
        }
        // Consistency check
        threads.emplace_back([this, &startLine, &finishLine]() {
            startLine.count_down();
            std::cout << "Consistency check thread started\n";
            for (auto _ = 0; _ < Solution::Count; ++_) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                checkConsistency();
            }
            std::cout << "Consistency check thread is done\n";
            finishLine.count_down();
        });
        assert(threads.size() == Count + 1);
    }

    // Gather all the threads and do any sort of cleanup necessary
    auto cleanup() -> void {
        for (auto &thread: threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }


    // Check some condition on the variables --> e.g. the sum of the primitives is constant, and some more
    auto checkConsistency() -> void {
        static const auto start = std::chrono::system_clock::now();
        const auto now = std::chrono::system_clock::now();
        std::cout << "Checking consistency -- " << (now - start).count() / 1000000 << " seconds elapsed\n";
        int totalSum = 0;
        std::vector<std::unique_lock<std::mutex>> locks;

        std::vector<Variable *> variablePointers;

        for (auto &var: variables) {
            variablePointers.push_back(&var);
        }

        std::sort(variablePointers.begin(), variablePointers.end());
        // if we get all the locks here, we'll lock ourselves out from reading the actual values, no?
        for (auto *varPtr: variablePointers) {
            locks.emplace_back(varPtr->getLock());
        }

        for (const auto &var: variables) {
            totalSum += var.getValue();
        }

        assert(totalSum == 0 && "The total sum of primitives should remain 0!");
    }

    // Update some of the primitives while also keeping consistency
    auto tick() -> void {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dist(0, Count / 2 - 1);

        std::cout << "Tick tack\n";
        int index1 = dist(gen);
        int index2 = dist(gen);
        if (index1 == index2) return;
        int delta = dist(gen);
        Variable &var1 = variables[index1];
        Variable &var2 = variables[index2];

        std::unique_lock<std::mutex> lock1(var1.getLock(), std::defer_lock);
        std::unique_lock<std::mutex> lock2(var2.getLock(), std::defer_lock);
        std::lock(lock1, lock2);

        var1.setValue(var1.getValue() - delta);
        var2.setValue(var2.getValue() + delta);
    }

};

int main() {
    auto &instance = Solution::Instantiate();
    instance.setup();
    instance.run();
    instance.cleanup();
    return 0;
}
