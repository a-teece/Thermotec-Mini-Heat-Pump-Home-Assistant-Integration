// SPDX-License-Identifier: GPL-3.0-or-later
//
// Minimal, dependency-free unit-test harness. Deliberately self-contained so
// the library's tests build with nothing but a C++17 compiler (no network
// fetch of GoogleTest/Catch2). Swap in a heavier framework later if desired —
// the TEST/CHECK surface is intentionally familiar.
#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace tf {

struct TestCase {
  std::string name;
  std::function<void()> fn;
};

inline std::vector<TestCase> &registry() {
  static std::vector<TestCase> r;
  return r;
}
inline int &failures() {
  static int f = 0;
  return f;
}

struct Registrar {
  Registrar(const std::string &n, std::function<void()> fn) {
    registry().push_back({n, std::move(fn)});
  }
};

inline std::string hex(const std::vector<uint8_t> &v) {
  std::string s;
  char b[4];
  for (auto x : v) {
    snprintf(b, sizeof b, "%02X ", x);
    s += b;
  }
  return s;
}

inline int run_all() {
  int passed = 0;
  for (auto &tc : registry()) {
    int before = failures();
    tc.fn();
    if (failures() == before) {
      passed++;
      printf("  PASS  %s\n", tc.name.c_str());
    } else {
      printf("  FAIL  %s\n", tc.name.c_str());
    }
  }
  printf("\n%d/%zu tests passed, %d checks failed\n", passed, registry().size(),
         failures());
  return failures() == 0 ? 0 : 1;
}

}  // namespace tf

#define TEST(name)                                              \
  static void name();                                           \
  static tf::Registrar reg_##name(#name, name);                 \
  static void name()

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      tf::failures()++;                                                   \
      printf("    CHECK failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
    }                                                                     \
  } while (0)

#define CHECK_EQ(a, b)                                                       \
  do {                                                                       \
    auto _a = (a);                                                           \
    auto _b = (b);                                                           \
    if (!(_a == _b)) {                                                       \
      tf::failures()++;                                                      \
      printf("    CHECK_EQ failed: %s == %s (%s:%d)\n", #a, #b, __FILE__,     \
             __LINE__);                                                      \
    }                                                                        \
  } while (0)

// Compare two byte vectors, printing both in hex on mismatch.
#define CHECK_BYTES(actual, expected)                                     \
  do {                                                                    \
    std::vector<uint8_t> _a = (actual);                                   \
    std::vector<uint8_t> _e = (expected);                                 \
    if (_a != _e) {                                                       \
      tf::failures()++;                                                   \
      printf("    CHECK_BYTES failed (%s:%d)\n      actual:   %s\n"        \
             "      expected: %s\n",                                      \
             __FILE__, __LINE__, tf::hex(_a).c_str(), tf::hex(_e).c_str()); \
    }                                                                     \
  } while (0)
