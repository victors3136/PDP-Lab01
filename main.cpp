
#include "VariableSystem.hpp"

#include <chrono>
#include <iostream>
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

#pragma clang diagnostic push
#pragma ide diagnostic ignored "cppcoreguidelines-avoid-magic-numbers"

auto main() -> int {
    const auto start = std::chrono::system_clock::now();
    VariableSystem system({
                                  {}                /*  0 */,
                                  {}                /*  1 */,
                                  {}                /*  2 */,
                                  {}                /*  3 */,
                                  {}                /*  4 */,
                                  {}                /*  5 */,
                                  {}                /*  6 */,
                                  {1,  0}                /*  7 */,
                                  {0,  1}                /*  8 */,
                                  {2,  3}                /*  9 */,
                                  {4,  5}                /*  10 */,
                                  {6,  7}                /*  11 */,
                                  {8,  9,  2, 2}          /*  12 */,
                                  {10, 11, 7}            /*  13 */,
                          });
    const auto end = std::chrono::system_clock::now();
    std::cout << "TOTAL EXECUTION TIME = " << std::chrono::duration_cast<std::chrono::seconds>(end - start)
              << " seconds\n";
    return 0;
}

#pragma clang diagnostic pop
