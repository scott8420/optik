#include "Undo.hpp"

#include <algorithm>
#include <stdexcept>

namespace optik::core {

// -- Edit: the only writer ----------------------------------------------------

void Edit::touch(const std::string& id) {
    if (m_before.count(id)) return;
    const Page* p = m_book.page(id);
    m_before[id] = p ? std::optional<Page>(*p) : std::nullopt;
}

Meta& Edit::meta() { return m_book.m_meta; }

Page& Edit::page(const std::string& id) {
    touch(id);
    for (auto& p : m_book.m_pages) if (p.id == id) return p;
    throw std::logic_error("Edit::page: no page " + id);
}

Page& Edit::insert_page(std::size_t at, Page p) {
    if (p.id.empty()) p.id = m_book.mint("pg_");
    touch(p.id);
    at = std::min(at, m_book.m_pages.size());
    return *m_book.m_pages.insert(m_book.m_pages.begin() + std::ptrdiff_t(at), std::move(p));
}

void Edit::remove_page(const std::string& id) {
    touch(id);
    auto& v = m_book.m_pages;
    v.erase(std::remove_if(v.begin(), v.end(), [&](const Page& p) { return p.id == id; }), v.end());
}

void Edit::move_page(const std::string& id, std::size_t to) {
    auto& v = m_book.m_pages;
    const int from = m_book.index_of(id);
    if (from < 0) return;
    Page p = std::move(v[std::size_t(from)]);
    v.erase(v.begin() + from);
    to = std::min(to, v.size());
    v.insert(v.begin() + std::ptrdiff_t(to), std::move(p));
}

const Scan& Edit::add_scan(Scan s) {
    if (s.id.empty()) s.id = m_book.mint("scn_");
    m_book.m_scans.push_back(std::move(s));
    return m_book.m_scans.back();
}

// -- Journal --------------------------------------------------------------------

namespace {
std::vector<std::string> order_of(const Book& b) {
    std::vector<std::string> v;
    v.reserve(b.pages().size());
    for (const auto& p : b.pages()) v.push_back(p.id);
    return v;
}
}  // namespace

bool Journal::run(Book& book, const std::string& label, const std::function<void(Edit&)>& fn) {
    Step s;
    s.label        = label;
    s.order_before = order_of(book);
    s.scans_before = book.m_scans;
    s.meta_before  = book.m_meta;

    Edit e(book);
    fn(e);

    s.order_after = order_of(book);
    s.scans_after = book.m_scans;
    s.meta_after  = book.m_meta;
    m_touched.clear();
    for (auto& [id, before] : e.m_before) {
        const Page* now = book.page(id);
        std::optional<Page> after = now ? std::optional<Page>(*now) : std::nullopt;
        if (after == before) continue;           // touched, not changed
        s.before[id] = std::move(before);
        s.after[id]  = std::move(after);
        m_touched.push_back(id);
    }
    if (s.before.empty() && s.order_before == s.order_after &&
        s.scans_before == s.scans_after && s.meta_before == s.meta_after)
        return false;

    m_steps.resize(m_pos);                       // a new step drops the redo tail
    if (m_mark_valid && m_mark > m_pos) m_mark_valid = false;
    m_steps.push_back(std::move(s));
    ++m_pos;
    return true;
}

void Journal::apply(Book& book, const std::map<std::string, std::optional<Page>>& pics,
                    const std::vector<std::string>& order, const std::vector<Scan>& scans,
                    const Meta& meta) {
    std::map<std::string, Page> by_id;
    for (auto& p : book.m_pages) by_id[p.id] = std::move(p);
    m_touched.clear();
    for (const auto& [id, pic] : pics) {
        if (pic) by_id[id] = *pic;
        else     by_id.erase(id);
        m_touched.push_back(id);
    }
    book.m_pages.clear();
    for (const auto& id : order) {
        auto it = by_id.find(id);
        if (it != by_id.end()) book.m_pages.push_back(std::move(it->second));
    }
    book.m_scans = scans;
    book.m_meta  = meta;
}

bool Journal::undo(Book& book) {
    if (!can_undo()) return false;
    const Step& s = m_steps[--m_pos];
    apply(book, s.before, s.order_before, s.scans_before, s.meta_before);
    return true;
}

bool Journal::redo(Book& book) {
    if (!can_redo()) return false;
    const Step& s = m_steps[m_pos++];
    apply(book, s.after, s.order_after, s.scans_after, s.meta_after);
    return true;
}

}  // namespace optik::core
