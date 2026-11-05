#pragma once
// core/Recent.hpp -- the books Optik has opened, newest first (s25).
//
// GTK-FREE. One small JSON file in the user's config folder
// ($XDG_CONFIG_HOME/optik/recent.json, else ~/.config/optik/recent.json). It is
// the app's memory, not the book's: no journal step, no undo. A missing or
// damaged file is an empty list, never an error the author has to deal with.
//
// The window reopens the newest book that still exists on a plain start, lists
// the rest in the menu, and remembers each book it opens.

#include "Book.hpp"

#include <string>
#include <vector>

namespace optik::core {

class Recent {
public:
    static constexpr std::size_t kMax = 10;

    static std::string default_file();

    explicit Recent(std::string file = default_file());

    void load();                                   // missing / damaged -> empty
    bool save() const;                             // creates the folder

    void touch(const std::string& book_folder);   // to the front, once, capped
    void forget(const std::string& book_folder);

    const std::vector<std::string>& all() const { return m_books; }
    std::vector<std::string> existing() const;    // those that still have book.json
    const std::string& file() const { return m_file; }

private:
    std::string              m_file;
    std::vector<std::string> m_books;
};

// Save As: the whole book folder (book.json + scans/) copied to `to`, which
// must not exist yet. Filesystem only -- run it on a worker; save the book first.
Status copy_book(const std::string& from, const std::string& to, std::string* why = nullptr);

}  // namespace optik::core
