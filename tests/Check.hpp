#pragma once
// The selftest's one reporting door, shared by every selftest TU.
#include <string>

namespace optik::test {
void check(const std::string& what, bool ok, const std::string& detail = "");
int  passes();
int  failures();
}  // namespace optik::test
