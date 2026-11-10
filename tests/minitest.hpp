#pragma once

// Tiny single-header unit test runner (no external deps).
// Usage:
//   TEST(suite, name) { CHECK_EQ(1, 1); }
//   int main() { return radio::test::run_all(); }

#include <cmath>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace radio::test {

struct Failure {
  std::string file;
  int line = 0;
  std::string message;
};

struct TestCase {
  const char* suite;
  const char* name;
  void (*fn)();
};

inline std::vector<TestCase>& registry() {
  static std::vector<TestCase> tests;
  return tests;
}

inline std::vector<Failure>& failures() {
  static std::vector<Failure> out;
  return out;
}

inline int& checks() {
  static int n = 0;
  return n;
}

struct Registrar {
  Registrar(const char* suite, const char* name, void (*fn)()) {
    registry().push_back(TestCase{suite, name, fn});
  }
};

inline void fail(const char* file, int line, const std::string& message) {
  failures().push_back(Failure{file, line, message});
}

inline int run_all() {
  int passed = 0;
  const auto& tests = registry();
  std::cout << "[radio-tests] running " << tests.size() << " test(s)\n";
  for (const auto& t : tests) {
    const std::size_t before = failures().size();
    try {
      t.fn();
    } catch (const std::exception& ex) {
      fail(__FILE__, __LINE__,
           std::string(t.suite) + "." + t.name + " threw: " + ex.what());
    } catch (...) {
      fail(__FILE__, __LINE__,
           std::string(t.suite) + "." + t.name + " threw unknown exception");
    }
    if (failures().size() == before) {
      ++passed;
      std::cout << "  PASS  " << t.suite << "." << t.name << "\n";
    } else {
      std::cout << "  FAIL  " << t.suite << "." << t.name << "\n";
      for (std::size_t i = before; i < failures().size(); ++i) {
        const auto& f = failures()[i];
        std::cout << "         " << f.file << ":" << f.line << "  " << f.message
                  << "\n";
      }
    }
  }
  const int failed = static_cast<int>(tests.size()) - passed;
  std::cout << "[radio-tests] " << passed << " passed, " << failed
            << " failed, " << checks() << " checks\n";
  return failed == 0 ? 0 : 1;
}

}  // namespace radio::test

#define RADIO_TEST_CONCAT2(a, b) a##b
#define RADIO_TEST_CONCAT(a, b) RADIO_TEST_CONCAT2(a, b)

#define TEST(suite, name)                                                     \
  static void RADIO_TEST_CONCAT(test_fn_##suite##_##name, __LINE__)();        \
  static ::radio::test::Registrar RADIO_TEST_CONCAT(                          \
      test_reg_##suite##_##name, __LINE__)(                                   \
      #suite, #name,                                                          \
      &RADIO_TEST_CONCAT(test_fn_##suite##_##name, __LINE__));                \
  static void RADIO_TEST_CONCAT(test_fn_##suite##_##name, __LINE__)()

#define CHECK(cond)                                                           \
  do {                                                                        \
    ++::radio::test::checks();                                                \
    if (!(cond)) {                                                            \
      ::radio::test::fail(__FILE__, __LINE__, "CHECK(" #cond ") failed");     \
    }                                                                         \
  } while (0)

#define CHECK_EQ(a, b)                                                        \
  do {                                                                        \
    ++::radio::test::checks();                                                \
    const auto& _ra = (a);                                                    \
    const auto& _rb = (b);                                                    \
    if (!(_ra == _rb)) {                                                      \
      std::ostringstream _oss;                                                \
      _oss << "CHECK_EQ(" #a ", " #b ") failed: " << _ra << " != " << _rb;    \
      ::radio::test::fail(__FILE__, __LINE__, _oss.str());                    \
    }                                                                         \
  } while (0)

#define CHECK_NE(a, b)                                                        \
  do {                                                                        \
    ++::radio::test::checks();                                                \
    const auto& _ra = (a);                                                    \
    const auto& _rb = (b);                                                    \
    if (_ra == _rb) {                                                         \
      std::ostringstream _oss;                                                \
      _oss << "CHECK_NE(" #a ", " #b ") failed: both are " << _ra;            \
      ::radio::test::fail(__FILE__, __LINE__, _oss.str());                    \
    }                                                                         \
  } while (0)

#define CHECK_TRUE(cond) CHECK(cond)
#define CHECK_FALSE(cond) CHECK(!(cond))

#define CHECK_NEAR(a, b, eps)                                                 \
  do {                                                                        \
    ++::radio::test::checks();                                                \
    const double _ra = static_cast<double>(a);                                \
    const double _rb = static_cast<double>(b);                                \
    const double _eps = static_cast<double>(eps);                             \
    if (std::fabs(_ra - _rb) > _eps) {                                        \
      std::ostringstream _oss;                                                \
      _oss << "CHECK_NEAR(" #a ", " #b ", " #eps ") failed: " << _ra          \
           << " vs " << _rb;                                                  \
      ::radio::test::fail(__FILE__, __LINE__, _oss.str());                    \
    }                                                                         \
  } while (0)
