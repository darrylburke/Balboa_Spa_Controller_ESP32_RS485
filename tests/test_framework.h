#pragma once
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>
#include <utility>

extern int g_checks;
extern int g_failures;

using TestFn = void (*)();
std::vector<std::pair<std::string, TestFn>> &test_registry();

struct TestRegistrar {
  TestRegistrar(const char *name, TestFn fn) { test_registry().push_back({name, fn}); }
};

#define TEST(name)                                            \
  static void name();                                         \
  static TestRegistrar registrar_##name(#name, name);         \
  static void name()

#define CHECK(cond)                                                          \
  do {                                                                       \
    g_checks++;                                                              \
    if (!(cond)) {                                                           \
      g_failures++;                                                          \
      std::printf("  FAIL %s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond);   \
    }                                                                        \
  } while (0)

#define CHECK_EQ(a, b)                                                       \
  do {                                                                       \
    g_checks++;                                                              \
    long long _a = (long long)(a);                                          \
    long long _b = (long long)(b);                                          \
    if (_a != _b) {                                                          \
      g_failures++;                                                          \
      std::printf("  FAIL %s:%d: CHECK_EQ(%s, %s) -> %lld != %lld\n",        \
                  __FILE__, __LINE__, #a, #b, _a, _b);                       \
    }                                                                        \
  } while (0)

// Parse a hex string (spaces/punctuation ignored) into bytes.
inline std::vector<uint8_t> hexb(const char *s) {
  std::vector<uint8_t> v;
  int hi = -1;
  for (; *s; ++s) {
    char c = *s;
    int d;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
    else continue;
    if (hi < 0) hi = d;
    else { v.push_back((uint8_t)((hi << 4) | d)); hi = -1; }
  }
  return v;
}
