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
    void set_thumb(const std::string& page_id, const core::Picture& pic);
    bool has_thumb(const std::string& page_id) const { return m_thumbs.count(page_id) > 0; }

    sigc::signal<void(int)> signal_page_selected;

    static constexpr int kThumbSide = 120;   // the longer side, px

private:
    struct Row;
    void fill(Row& r, const core::Book& book, std::size_t index);

    widgets::ListBox m_list;
    std::vector<std::unique_ptr<Row>> m_rows;
    std::map<std::string, Cairo::RefPtr<Cairo::ImageSurface>> m_thumbs;
    bool m_quiet = false;
};

}  // namespace optik
