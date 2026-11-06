// optik_selftest -- headless: libs/scan, libs/ocr, core. No display.
//
//   ./build/optik_selftest      exit 0 = all pass
//
// It gates Scott's launch (rm -rf build && ./build.sh && ./build/optik_selftest
// && build/optik), so every test asserts a RULE that holds on any machine --
// never a layout or a reading only this sandbox's Tesseract would produce.
#include "Check.hpp"
#include "ocr/Ocr.hpp"

#include <iostream>

void scan_tests();
void core_tests();
void passes_tests();
void recent_tests();
void audit_tests();
void pageops_tests();
void italic_tests();
void spelling_tests();
void find_tests();
void format_tests();

namespace optik::test {
static int g_pass = 0, g_fail = 0;
void check(const std::string& what, bool ok, const std::string& detail) {
    if (ok) { ++g_pass; std::cout << "  ok   " << what; }
    else    { ++g_fail; std::cout << "  FAIL " << what; }
    if (!detail.empty()) std::cout << "   [" << detail << "]";
    std::cout << "\n";
}
int passes()   { return g_pass; }
int failures() { return g_fail; }
}  // namespace optik::test

int main() {
    std::cout << "optik_selftest " OPTIK_BUILD " -- tesseract " << optik::ocr::Engine::version() << "\n";
    std::cout << "---------------------------------\n";
    scan_tests();
    core_tests();
    passes_tests();
    recent_tests();
    audit_tests();
    pageops_tests();
    italic_tests();
    spelling_tests();
    find_tests();
    format_tests();
    std::cout << "---------------------------------\n";
    std::cout << optik::test::passes() << " pass / " << optik::test::failures() << " fail\n";
    return optik::test::failures() == 0 ? 0 : 1;
}
