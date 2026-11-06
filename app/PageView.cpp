#include "PageView.hpp"

#include "core/Audit.hpp"
#include "core/PageOps.hpp"

#include "Css.hpp"

#include <gtkmm/eventcontrollermotion.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace optik {

const ocr::Word* word_at(const core::Page& page, const WordRef& r) {
    if (!r.valid() || r.area >= int(page.areas.size())) return nullptr;
    const auto& a = page.areas[std::size_t(r.area)];
    if (r.para >= int(a.paras.size())) return nullptr;
    const auto& p = a.paras[std::size_t(r.para)];
    if (r.line >= int(p.lines.size())) return nullptr;
    const auto& l = p.lines[std::size_t(r.line)];
    if (r.word >= int(l.words.size())) return nullptr;
    return &l.words[std::size_t(r.word)];
}

namespace {

const css::Rgba& colour_of(ocr::Kind k) {
    switch (k) {
        case ocr::Kind::Text:    return css::kText;
        case ocr::Kind::Heading: return css::kHeading;
        case ocr::Kind::Picture: return css::kPicture;
        case ocr::Kind::Header:
        case ocr::Kind::Footer:  return css::kChrome;
        case ocr::Kind::Ignore:  return css::kIgnore;
    }
    return css::kText;
}

void rgba(const Cairo::RefPtr<Cairo::Context>& cr, const css::Rgba& c, double alpha_mul = 1.0) {
    cr->set_source_rgba(c.r, c.g, c.b, c.a * alpha_mul);
}

constexpr double kPad   = 16.0;   // around the page, view px
constexpr double kGrip  = 8.0;    // how near an edge counts as on it, view px

}  // namespace

PageView::PageView() : widgets::DrawingArea("view.page") {
    add_css_class("optik-pageview");
    set_expand(true);
    set_focusable(true);
    set_draw_func(sigc::mem_fun(*this, &PageView::on_draw));

    m_click = Gtk::GestureClick::create();
    m_click->signal_pressed().connect(sigc::mem_fun(*this, &PageView::on_press));
    add_controller(m_click);

    m_drag = Gtk::GestureDrag::create();
    m_drag->signal_drag_begin().connect(sigc::mem_fun(*this, &PageView::on_drag_begin));
    m_drag->signal_drag_update().connect(sigc::mem_fun(*this, &PageView::on_drag_update));
    m_drag->signal_drag_end().connect(sigc::mem_fun(*this, &PageView::on_drag_end));
    add_controller(m_drag);

    // Middle-drag pans; the wheel scrolls; Ctrl+wheel zooms about the pointer.
    m_pan = Gtk::GestureDrag::create();
    m_pan->set_button(GDK_BUTTON_MIDDLE);
    m_pan->signal_drag_begin().connect([this](double, double) { m_pan_x0 = m_pan_x; m_pan_y0 = m_pan_y; });
    m_pan->signal_drag_update().connect([this](double dx, double dy) {
        m_pan_x = m_pan_x0 - dx;
        m_pan_y = m_pan_y0 - dy;
        queue_draw();
    });
    add_controller(m_pan);

    auto scroll = Gtk::EventControllerScroll::create();
    scroll->set_flags(Gtk::EventControllerScroll::Flags::BOTH_AXES);
    scroll->signal_scroll().connect(sigc::mem_fun(*this, &PageView::on_scroll), false);
    m_scroll = scroll;
    add_controller(scroll);

    auto motion = Gtk::EventControllerMotion::create();
    motion->signal_motion().connect([this](double x, double y) { m_px = x; m_py = y; m_pointer_in = true; });
    motion->signal_leave().connect([this] { m_pointer_in = false; });
    add_controller(motion);
}

void PageView::show(const core::Book* book, const core::Page* page) {
    const bool other = !page || !m_page || page->id != m_page->id;
    m_book = book;
    m_page = page;
    if (other) { m_word = {}; m_area = -1; }
    if (page && core::look_key(*page) != m_pic_key) { m_surface.reset(); m_pic.reset(); }
    if (other) m_pan_x = m_pan_y = 0;   // a new page starts at its top
    queue_draw();
}

void PageView::set_picture(const std::string& key, std::shared_ptr<const core::Picture> pic) {
    if (!pic || !pic->valid()) return;
    m_pic_key = key;
    m_pic = std::move(pic);
    m_surface = Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32, m_pic->width, m_pic->height);
    m_surface->flush();
    unsigned char* dst = m_surface->get_data();
    const int stride = m_surface->get_stride();
    for (int y = 0; y < m_pic->height; ++y)
        std::memcpy(dst + std::size_t(y) * stride, m_pic->px.data() + std::size_t(y) * m_pic->width,
                    std::size_t(m_pic->width) * 4);
    m_surface->mark_dirty();
    queue_draw();
}

void PageView::set_word(const WordRef& w) { m_word = w; queue_draw(); }
void PageView::set_area(int a)            { m_area = a; queue_draw(); }
void PageView::set_tool(Tool t) {
    m_tool = t;
    set_cursor(t == Tool::Draw ? "crosshair" : "default");
}

// Where the page sits: scale s (view px per page px) and origin (ox, oy). On an
// axis where the page fits it is centred; where it does not, the pan picks the
// part on show and is clamped so the page never leaves the pane.
bool PageView::layout(double& s, double& ox, double& oy) const {
    if (!m_page || m_page->crop.empty()) return false;
    const double vw = get_width(), vh = get_height();
    const core::Size2i ps = core::page_size(*m_page);    // the turned crop (s26)
    const double cw = ps.w, ch = ps.h;
    if (vw <= 2 * kPad || vh <= 2 * kPad) return false;
    s = m_scale;
    if (m_fit == Fit::Page)  s = std::min((vw - 2 * kPad) / cw, (vh - 2 * kPad) / ch);
    if (m_fit == Fit::Width) s = (vw - 2 * kPad) / cw;
    auto axis = [](double view, double content, double& pan) {
        const double room = content + 2 * kPad - view;
        if (room <= 0) { pan = 0; return (view - content) / 2.0; }
        pan = std::clamp(pan, 0.0, room);
        return kPad - pan;
    };
    ox = axis(vw, s * cw, m_pan_x);
    oy = axis(vh, s * ch, m_pan_y);
    return true;
}

double PageView::zoom() const {
    double s, ox, oy;
    return layout(s, ox, oy) ? s : 0.0;
}

void PageView::set_fit(Fit f) {
    m_fit = f;
    m_pan_x = m_pan_y = 0;
    queue_draw();
    signal_zoom_changed.emit();
}

void PageView::zoom_by(double factor) { zoom_at(factor, get_width() / 2.0, get_height() / 2.0); }

// Zoom keeping the page point under (x, y) where it is.
void PageView::zoom_at(double factor, double x, double y) {
    double s, ox, oy;
    if (!layout(s, ox, oy)) return;
    const double px = (x - ox) / s, py = (y - oy) / s;
    m_scale = std::clamp(s * factor, 0.05, 8.0);
    m_fit = Fit::Free;
    m_pan_x = kPad - (x - px * m_scale);
    m_pan_y = kPad - (y - py * m_scale);
    queue_draw();
    signal_zoom_changed.emit();
}

bool PageView::on_scroll(double dx, double dy) {
    auto ev = std::dynamic_pointer_cast<Gtk::EventControllerScroll>(m_scroll);
    const auto mods = ev ? ev->get_current_event_state() : Gdk::ModifierType{};
    if ((mods & Gdk::ModifierType::CONTROL_MASK) == Gdk::ModifierType::CONTROL_MASK) {
        double x = get_width() / 2.0, y = get_height() / 2.0;
        if (m_pointer_in) { x = m_px; y = m_py; }
        zoom_at(dy < 0 ? 1.15 : 1.0 / 1.15, x, y);
        return true;
    }
    if ((mods & Gdk::ModifierType::SHIFT_MASK) == Gdk::ModifierType::SHIFT_MASK) std::swap(dx, dy);
    m_pan_x += dx * 60.0;
    m_pan_y += dy * 60.0;
    queue_draw();
    return true;
}

double PageView::to_page_x(double x) const {
    double s, ox, oy;
    return layout(s, ox, oy) ? (x - ox) / s : 0.0;
}
double PageView::to_page_y(double y) const {
    double s, ox, oy;
    return layout(s, ox, oy) ? (y - oy) / s : 0.0;
}

void PageView::on_draw(const Cairo::RefPtr<Cairo::Context>& cr, int, int) {
    double s, ox, oy;
    if (!layout(s, ox, oy)) return;
    const core::Size2i ps = core::page_size(*m_page);

    // The paper, then the scan (or plain paper while it loads).
    cr->save();
    cr->translate(ox, oy);
    cr->rectangle(0, 0, s * ps.w, s * ps.h);
    cr->set_source_rgb(1, 1, 1);
    cr->fill();
    if (m_surface && m_pic && m_pic_key == core::look_key(*m_page)) {
        cr->save();
        cr->scale(s / m_pic->scale, s / m_pic->scale);
        auto pat = Cairo::SurfacePattern::create(m_surface);
        pat->set_filter(Cairo::SurfacePattern::Filter::GOOD);
        cr->set_source(pat);
        cr->paint();
        cr->restore();
    }

    auto rect = [&](const core::Rect& r) {
        cr->rectangle(s * r.x0, s * r.y0, s * r.width(), s * r.height());
    };

    // Uncertain words first, so the area outlines sit over them.
    for (std::size_t ai = 0; ai < m_page->areas.size(); ++ai) {
        const auto& a = m_page->areas[ai];
        if (!ocr::kind_is_text(a.kind)) continue;
        for (const auto& p : a.paras)
            for (const auto& l : p.lines)
                for (const auto& w : l.words)
                    if (m_book && m_book->uncertain(w)) { rect(w.box); rgba(cr, css::kUncertain); cr->fill(); }
    }

    for (std::size_t ai = 0; ai < m_page->areas.size(); ++ai) {
        const auto& a = m_page->areas[ai];
        core::Rect box = a.box;
        if (int(ai) == m_area && m_dragging && m_edges) box = drag_rect();
        rect(box);
        rgba(cr, colour_of(a.kind), 0.07);
        cr->fill_preserve();
        rgba(cr, colour_of(a.kind), int(ai) == m_area ? 1.0 : 0.75);
        cr->set_line_width(int(ai) == m_area ? 3.0 : 1.5);
        cr->stroke();
        if (int(ai) == m_area) {
            const double hs = 4.0;
            for (double x : {double(box.x0), double(box.x1)})
                for (double y : {double(box.y0), double(box.y1)})
                    cr->rectangle(s * x - hs, s * y - hs, 2 * hs, 2 * hs);
            cr->fill();
        }
    }

    if (m_word.valid()) {
        if (const ocr::Word* w = word_at(*m_page, m_word)) {
            rect(w->box);
            rgba(cr, css::kCurrent);
            cr->set_line_width(2.5);
            cr->stroke();
        }
    }

    if (m_dragging && !m_edges && m_tool == Tool::Draw) {
        rect(drag_rect());
        rgba(cr, css::kText, 0.15);
        cr->fill_preserve();
        rgba(cr, css::kText);
        cr->set_dash(std::vector<double>{6.0, 4.0}, 0.0);
        cr->set_line_width(2.0);
        cr->stroke();
    }
    cr->restore();
}

void PageView::on_press(int, double x, double y) {
    grab_focus();
    if (!m_page || m_tool == Tool::Draw) return;
    const int px = int(to_page_x(x)), py = int(to_page_y(y));
    // On the half of the selected area's edge grip that lies just OUTSIDE its
    // box, the press is the start of a resize: keep the selection. (s26: the
    // sweep found it deselected, so the drag that followed had nothing to
    // resize.) Inside the box a press still picks the word under it -- the
    // first word of every line sits on the left grip.
    if (grip_edges(x, y) && !m_page->areas[std::size_t(m_area)].box.contains(px, py)) return;

    for (std::size_t ai = 0; ai < m_page->areas.size(); ++ai) {
        const auto& a = m_page->areas[ai];
        for (std::size_t pi = 0; pi < a.paras.size(); ++pi)
            for (std::size_t li = 0; li < a.paras[pi].lines.size(); ++li) {
                const auto& ws = a.paras[pi].lines[li].words;
                for (std::size_t wi = 0; wi < ws.size(); ++wi)
                    if (ws[wi].box.contains(px, py)) {
                        m_area = int(ai);
                        m_word = WordRef{int(ai), int(pi), int(li), int(wi)};
                        queue_draw();
                        signal_area_selected.emit(m_area);
                        signal_word_clicked.emit(m_word);
                        return;
                    }
            }
    }
    // Not on a word: the smallest area under the pointer.
    int best = -1;
    long best_size = 0;
    for (std::size_t ai = 0; ai < m_page->areas.size(); ++ai) {
        const auto& b = m_page->areas[ai].box;
        const long size = long(b.width()) * b.height();
        if (b.contains(px, py) && (best < 0 || size < best_size)) { best = int(ai); best_size = size; }
    }
    if (best != m_area) {
        m_area = best;
        queue_draw();
        signal_area_selected.emit(m_area);
    }
}

void PageView::on_drag_begin(double x, double y) {
    m_x0 = x; m_y0 = y; m_dx = m_dy = 0;
    m_edges = 0;
    m_dragging = false;
    if (!m_page) return;
    if (m_tool == Tool::Draw) { m_dragging = true; return; }
    if (m_area < 0 || m_area >= int(m_page->areas.size())) return;

    m_edges = grip_edges(x, y);
    m_dragging = m_edges != 0;
    if (m_dragging) m_drag->set_state(Gtk::EventSequenceState::CLAIMED);
}

void PageView::on_drag_update(double dx, double dy) {
    if (!m_dragging) return;
    m_dx = dx; m_dy = dy;
    queue_draw();
}

core::Rect PageView::drag_rect() const {
    if (!m_page) return {};
    const double x0 = to_page_x(m_x0), y0 = to_page_y(m_y0);
    const double x1 = to_page_x(m_x0 + m_dx), y1 = to_page_y(m_y0 + m_dy);
    core::Rect r;
    if (m_edges && m_area >= 0 && m_area < int(m_page->areas.size())) {
        r = m_page->areas[std::size_t(m_area)].box;
        const int ddx = int(std::lround(x1 - x0)), ddy = int(std::lround(y1 - y0));
        if (m_edges & 1) r.x0 += ddx;
        if (m_edges & 2) r.x1 += ddx;
        if (m_edges & 4) r.y0 += ddy;
        if (m_edges & 8) r.y1 += ddy;
    } else {
        r = core::Rect{int(std::min(x0, x1)), int(std::min(y0, y1)),
                       int(std::max(x0, x1)), int(std::max(y0, y1))};
    }
    // Normalise and keep it on the page.
    if (r.x0 > r.x1) std::swap(r.x0, r.x1);
    if (r.y0 > r.y1) std::swap(r.y0, r.y1);
    const core::Size2i ps = core::page_size(*m_page);
    r.x0 = std::clamp(r.x0, 0, ps.w);
    r.x1 = std::clamp(r.x1, 0, ps.w);
    r.y0 = std::clamp(r.y0, 0, ps.h);
    r.y1 = std::clamp(r.y1, 0, ps.h);
    return r;
}

void PageView::on_drag_end(double dx, double dy) {
    if (!m_dragging) return;
    m_dx = dx; m_dy = dy;
    const core::Rect r = drag_rect();
    const int edges = m_edges;
    m_dragging = false;
    m_edges = 0;
    queue_draw();
    if (r.width() < 8 || r.height() < 8) return;   // a click, not a drag
    if (edges) signal_area_resized.emit(m_area, r);
    else       signal_area_drawn.emit(r);
}

// Which edges of the selected area (view px x, y) is on: bit 1 left, 2 right,
// 4 top, 8 bottom; 0 when none or nothing is selected.
int PageView::grip_edges(double x, double y) const {
    if (!m_page || m_area < 0 || m_area >= int(m_page->areas.size())) return 0;
    double s, ox, oy;
    if (!layout(s, ox, oy)) return 0;
    const auto& b = m_page->areas[std::size_t(m_area)].box;
    const double l = ox + s * b.x0, r = ox + s * b.x1, t = oy + s * b.y0, btm = oy + s * b.y1;
    const bool in_y = y > t - kGrip && y < btm + kGrip;
    const bool in_x = x > l - kGrip && x < r + kGrip;
    int e = 0;
    if (in_y && std::abs(x - l) < kGrip)   e |= 1;
    if (in_y && std::abs(x - r) < kGrip)   e |= 2;
    if (in_x && std::abs(y - t) < kGrip)   e |= 4;
    if (in_x && std::abs(y - btm) < kGrip) e |= 8;
    return e;
}

// -- the sweep's hands (s26) --------------------------------------------------------

void PageView::poke_drag(double x0, double y0, double x1, double y1) {
    on_press(1, x0, y0);
    on_drag_begin(x0, y0);
    on_drag_update((x1 - x0) / 2, (y1 - y0) / 2);
    on_drag_end(x1 - x0, y1 - y0);
}

bool PageView::page_to_view(double px, double py, double& x, double& y) const {
    double s, ox, oy;
    if (!layout(s, ox, oy)) return false;
    x = ox + s * px;
    y = oy + s * py;
    return true;
}

}  // namespace optik
