#pragma once
// PagesPanel -- every page of the book as a thumbnail with its state: not read,
// read, or how many words want a look. FineReader's Pages pane.
//
// Rebuilt when the page list changes (import, undo of an import); a single row
// is refreshed when its page changes. Thumbnails arrive later, from a worker.

#include "core/Book.hpp"
#include "core/Picture.hpp"
#include "widgets/Widgets.hpp"

#include <cairomm/surface.h>
#include <sigc++/signal.h>

#include <map>
#include <memory>

namespace optik {

class PagesPanel : public widgets::ScrolledWindow {
public:
    PagesPanel();
    ~PagesPanel() override;

    void rebuild(const core::Book& book);             // page list changed
    void refresh(const core::Book& book, const std::string& page_id);
    void select(int index);                           // no signal
    // Keyed by core::look_key (s26): a re-cut or turned page's old thumb is stale.
    void set_thumb(const std::string& page_id, const std::string& look_key, const core::Picture& pic);
    bool has_thumb(const std::string& page_id, const std::string& look_key) const {
        auto it = m_thumb_keys.find(page_id);
        return it != m_thumb_keys.end() && it->second == look_key;
    }
    std::size_t rows() const { return m_rows.size(); }   // the sweep: the pane matches the book

    sigc::signal<void(int)> signal_page_selected;

    static constexpr int kThumbSide = 120;   // the longer side, px

private:
    struct Row;
    void fill(Row& r, const core::Book& book, std::size_t index);

    widgets::ListBox m_list;
    std::vector<std::unique_ptr<Row>> m_rows;
    std::map<std::string, Cairo::RefPtr<Cairo::ImageSurface>> m_thumbs;
    std::map<std::string, std::string> m_thumb_keys;
    bool m_quiet = false;
};

}  // namespace optik
