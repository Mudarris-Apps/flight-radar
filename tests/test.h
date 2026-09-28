#pragma once
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <functional>
struct TestCase { const char *name; std::function<void()> fn; };
inline std::vector<TestCase> &tests() { static std::vector<TestCase> t; return t; }
inline int &failures() { static int f = 0; return f; }
struct TestRegistrar { TestRegistrar(const char *n, std::function<void()> f) { tests().push_back({n, f}); } };
#define TEST(name) static void name(); static TestRegistrar name##_reg(#name, name); static void name()
#define CHECK(expr) do { if (!(expr)) { std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); failures()++; } } while (0)
#define CHECK_NEAR(a, b, eps) do { double _a=(a), _b=(b); if (std::fabs(_a-_b) > (eps)) { std::printf("  FAIL %s:%d: %s=%g vs %s=%g\n", __FILE__, __LINE__, #a, _a, #b, _b); failures()++; } } while (0)
#define CHECK_STREQ(a, b) do { if (std::strcmp((a),(b)) != 0) { std::printf("  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); failures()++; } } while (0)
inline int runAllTests() {
  for (auto &t : tests()) { std::printf("[ RUN ] %s\n", t.name); t.fn(); }
  std::printf("%zu tests, %d failures\n", tests().size(), failures());
  return failures() ? 1 : 0;
}
#define TEST_MAIN() int main() { return runAllTests(); }
