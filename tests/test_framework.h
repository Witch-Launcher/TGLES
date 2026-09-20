#ifndef TGLES_TEST_FRAMEWORK_H
#define TGLES_TEST_FRAMEWORK_H

// Minimal dependency-free test framework (English identifiers).
// Usage: TEST(SuiteName, CaseName) { EXPECT_TRUE(...); }

#include <cstdio>
#include <cstring>
#include <vector>

namespace tgles_test {

struct TestCase {
  const char* suite;
  const char* name;
  void (*func)();
};

inline std::vector<TestCase>& Registry() {
  static std::vector<TestCase> registry;
  return registry;
}

inline int& FailureCount() {
  static int count = 0;
  return count;
}

inline int& CheckCount() {
  static int count = 0;
  return count;
}

inline void Register(const char* suite, const char* name, void (*func)()) {
  Registry().push_back(TestCase{suite, name, func});
}

inline int RunAll() {
  int failed_cases = 0;
  for (const auto& t : Registry()) {
    int before = FailureCount();
    t.func();
    int after = FailureCount();
    if (after == before) {
      std::printf("[  PASS ] %s.%s\n", t.suite, t.name);
    } else {
      std::printf("[  FAIL ] %s.%s (%d checks failed)\n", t.suite, t.name,
                  after - before);
      ++failed_cases;
    }
  }
  std::printf("Ran %lu tests, %d checks, %d failures (%d failing cases).\n",
              (unsigned long)Registry().size(), CheckCount(), FailureCount(),
              failed_cases);
  return failed_cases == 0 && FailureCount() == 0 ? 0 : 1;
}

struct Registrar {
  Registrar(const char* suite, const char* name, void (*func)()) {
    Register(suite, name, func);
  }
};

}  // namespace tgles_test

#define TEST(suite, name)                                                  \
  static void tgles_test_body_##suite##_##name();                          \
  static tgles_test::Registrar tgles_test_reg_##suite##_##name(            \
      #suite, #name, &tgles_test_body_##suite##_##name);                   \
  static void tgles_test_body_##suite##_##name()

#define EXPECT_TRUE(cond)                                                  \
  do {                                                                     \
    ++tgles_test::CheckCount();                                            \
    if (!(cond)) {                                                         \
      ++tgles_test::FailureCount();                                        \
      std::printf("  failure at %s:%d: EXPECT_TRUE(%s)\n", __FILE__,       \
                  __LINE__, #cond);                                        \
    }                                                                      \
  } while (0)

#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))

#define EXPECT_EQ(a, b)                                                    \
  do {                                                                     \
    ++tgles_test::CheckCount();                                            \
    if (!((a) == (b))) {                                                   \
      ++tgles_test::FailureCount();                                        \
      std::printf("  failure at %s:%d: EXPECT_EQ(%s, %s)\n", __FILE__,     \
                  __LINE__, #a, #b);                                       \
    }                                                                      \
  } while (0)

#define EXPECT_NE(a, b)                                                    \
  do {                                                                     \
    ++tgles_test::CheckCount();                                            \
    if (!((a) != (b))) {                                                   \
      ++tgles_test::FailureCount();                                        \
      std::printf("  failure at %s:%d: EXPECT_NE(%s, %s)\n", __FILE__,     \
                  __LINE__, #a, #b);                                       \
    }                                                                      \
  } while (0)

#define EXPECT_STREQ(a, b)                                                 \
  do {                                                                     \
    ++tgles_test::CheckCount();                                            \
    if (std::strcmp((a), (b)) != 0) {                                      \
      ++tgles_test::FailureCount();                                        \
      std::printf("  failure at %s:%d: EXPECT_STREQ(%s, %s)\n", __FILE__,  \
                  __LINE__, #a, #b);                                       \
    }                                                                      \
  } while (0)

#define EXPECT_NOT_NULL(p) EXPECT_TRUE((p) != nullptr)

#endif  // TGLES_TEST_FRAMEWORK_H
