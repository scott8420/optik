#include "PagesPanel.hpp"

#include <cstring>

namespace optik {

struct PagesPanel::Row {
    std::string id;
    widgets::Box box{widgets::unregistered, "pages.row", Gtk::Orientation::HORIZONTAL, 8};
    widgets::DrawingArea thumb{widgets::unregistered, "pages.row.thumb"};
    widgets::Box text{widgets::unregistered, "pages.row.text", Gtk::Orientation::VERTICAL, 2};
    widgets::Label title{widgets::unregistered, "pages.row.title"};
    widgets::Label status{widgets::unregistered, "pages.row.status"};
    Cairo::RefPtr<Cairo::ImageSurface> surface;
};

PagesPanel::PagesPanel() : widgets::ScrolledWindow("pages.scroll"), m_list("pages.list") {
    set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    set_size_request(200, -1);
    m_list.set_selection_mode(Gtk::SelectionMode::SINGLE);
    set_child(m_list);
    m_list.signal_row_selected().connect([this](Gtk::ListBoxRow* row) {
        if (m_quiet || !row) return;
        signal_page_selected.emit(row->get_index());
    });
}

PagesPanel::~PagesPanel() = default;

void PagesPanel::fill(Row& r, const core::Book& book, std::size_t i) {
    const core::Page& p = book.pages()[i];
    r.id = p.id;
    std::string title = "Page " + std::to_string(i + 1);
    if (p.half == model::split::Half::Left)  title += "  (left)";
    if (p.half == model::split::Half::Right) title += "  (right)";
    r.title.set_text(title);
    if (!p.read) {
        r.status.set_text("not read");
        r.status.set_css_classes({"optik-pagerow-status"});
    } else {
        const int n = book.uncertain_count(p);
        r.status.set_text(n ? std::to_string(n) + (n == 1 ? " word to check" : " words to check") : "read");
        r.status.set_css_classes({n ? "optik-pagerow-check" : "optik-pagerow-status"});
    }
    auto it = m_thumbs.find(p.id);
    r.surface = it != m_thumbs.end() ? it->second : Cairo::RefPtr<Cairo::ImageSurface>();
    r.thumb.queue_draw();
}

void PagesPanel::rebuild(const core::Book& book) {
    m_quiet = true;
    while (auto* row = m_list.get_row_at_index(0)) m_list.remove(*row);
    m_rows.clear();
    for (std::size_t i = 0; i < book.pages().size(); ++i) {
        auto r = std::make_unique<Row>();
        Row* rp = r.get();
        r->thumb.set_content_width(kThumbSide * 3 / 4);
        r->thumb.set_content_height(kThumbSide);
        r->thumb.set_draw_func([rp](const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
            if (!rp->surface) {
                cr->set_source_rgb(0.85, 0.85, 0.85);
                cr->rectangle(4, 4, w - 8, h - 8);
                cr->fill();
                return;
            }
            const double sw = rp->surface->get_width(), sh = rp->surface->get_height();
            const double s = std::min(w / sw, h / sh);
            cr->translate((w - s * sw) / 2, (h - s * sh) / 2);
            cr->scale(s, s);
            cr->set_source(rp->surface, 0, 0);
            cr->paint();
        });
        r->title.add_css_class("optik-pagerow-title");
        r->title.set_xalign(0);
        r->status.set_xalign(0);
        r->text.set_valign(Gtk::Align::CENTER);
        r->text.append(r->title);
        r->text.append(r->status);
        r->box.set_margin(4);
        r->box.append(r->thumb);
        r->box.append(r->text);
        fill(*r, book, i);
        m_list.append(r->box);
        m_rows.push_back(std::move(r));
    }
    m_quiet = false;
}

void PagesPanel::refresh(const core::Book& book, const std::string& id) {
    const int i = book.index_of(id);
    if (i < 0 || i >= int(m_rows.size()) || m_rows[std::size_t(i)]->id != id) return;
    fill(*m_rows[std::size_t(i)], book, std::size_t(i));
}

void PagesPanel::select(int index) {
    m_quiet = true;
    if (auto* row = m_list.get_row_at_index(index)) {
        m_list.select_row(*row);
        // Bring it into view without taking the keyboard from the text pane.
        const auto adj = get_vadjustment();
        const auto al = row->get_allocation();
        if (adj && al.get_height() > 0) {
            if (al.get_y() < adj->get_value())
                adj->set_value(al.get_y());
            else if (al.get_y() + al.get_height() > adj->get_value() + adj->get_page_size())
                adj->set_value(al.get_y() + al.get_height() - adj->get_page_size());
        }
    } else {
        m_list.unselect_all();
    }
    m_quiet = false;
}

void PagesPanel::set_thumb(const std::string& id, const core::Picture& pic) {
    if (!pic.valid()) return;
    auto surf = Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32, pic.width, pic.height);
    surf->flush();
    unsigned char* dst = surf->get_data();
    for (int y = 0; y < pic.height; ++y)
        std::memcpy(dst + std::size_t(y) * surf->get_stride(), pic.px.data() + std::size_t(y) * pic.width,
                    std::size_t(pic.width) * 4);
    surf->mark_dirty();
    m_thumbs[id] = surf;
    for (auto& r : m_rows)
        if (r->id == id) { r->surface = surf; r->thumb.queue_draw(); }
}

}  // namespace optik
