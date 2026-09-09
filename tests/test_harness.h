#pragma once

#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

// Minimal dependency-free test harness. Kept intentionally small so the test
// suite has no third-party dependencies (see the dependency policy in the
// product spec).

namespace fastpdf::test {

struct TestCase {
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& Registry() {
    static std::vector<TestCase> registry;
    return registry;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { Registry().push_back({name, fn}); }
};

inline int RunAll() {
    int failed = 0;
    for (const TestCase& test : Registry()) {
        try {
            test.fn();
            std::printf("[PASS] %s\n", test.name);
        } catch (const std::exception& e) {
            ++failed;
            std::printf("[FAIL] %s: %s\n", test.name, e.what());
        } catch (...) {
            ++failed;
            std::printf("[FAIL] %s: unknown exception\n", test.name);
        }
    }
    std::printf("%zu test(s), %d failed\n", Registry().size(), failed);
    return failed == 0 ? 0 : 1;
}

} // namespace fastpdf::test

#define FASTPDF_TEST(name)                                                    \
    static void name();                                                       \
    static ::fastpdf::test::Registrar name##_registrar(#name, &name);         \
    static void name()

#define FASTPDF_CHECK(cond)                                                   \
    do {                                                                      \
        if (!(cond)) {                                                        \
            throw std::runtime_error(std::string("CHECK failed: ") + #cond +  \
                                     " (" + __FILE__ + ":" +                  \
                                     std::to_string(__LINE__) + ")");         \
        }                                                                     \
    } while (0)

#define FASTPDF_CHECK_EQ(a, b)                                                \
    do {                                                                      \
        const auto& lhs = (a);                                                \
        const auto& rhs = (b);                                                \
        if (!(lhs == rhs)) {                                                  \
            throw std::runtime_error(std::string("CHECK_EQ failed: ") + #a +  \
                                     " == " + #b + " (" + __FILE__ + ":" +    \
                                     std::to_string(__LINE__) + ")");         \
        }                                                                     \
    } while (0)