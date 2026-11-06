// optik -- an OCR workbench for scanned books.
//   optik                      empty window
//   optik <book.optik>         open a book
//   optik <folder of scans>    make a book from them (beside the folder) and read it
#include "App.hpp"
#include "Shell.hpp"

int main(int argc, char* argv[]) {
    auto app = optik::App::create();
    const int rc = app->run(argc, argv);
    // OPTIK_SWEEP=1 (scripts/sweep.sh): the exit code is the sweep's verdict.
    return optik::sweep_status() >= 0 ? optik::sweep_status() : rc;
}
