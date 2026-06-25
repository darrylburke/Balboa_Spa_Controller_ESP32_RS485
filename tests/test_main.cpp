#include "test_framework.h"

int g_checks = 0;
int g_failures = 0;

std::vector<std::pair<std::string, TestFn>> &test_registry() {
  static std::vector<std::pair<std::string, TestFn>> r;
  return r;
}

int main() {
  for (auto &t : test_registry()) {
    std::printf("RUN %s\n", t.first.c_str());
    t.second();
  }
  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
