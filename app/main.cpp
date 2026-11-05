// optik -- an OCR workbench for scanned books.
//   optik                      empty window
//   optik <book.optik>         open a book
//   optik <folder of scans>    make a book from them (beside the folder) and read it
#include "App.hpp"

int main(int argc, char* argv[]) {
    auto app = optik::App::create();
    return app->run(argc, argv);
}
