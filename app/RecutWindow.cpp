#include "RecutWindow.hpp"

#include <gtkmm/gestureclick.h>
#include <gtkmm/gesturedrag.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace optik {

namespace {
constexpr double kPad = 12.0;
}

RecutWindow::RecutWindow() {
    set_name("recut");
    set_title("Re-cut the Scan");
    set_modal(true);
    set_hide_on_close(true);
    set_default_size(760, 640);

    m_help.set_text("One page, or two with the cut where the gutter is: click or drag on the scan.\n"
                    "The pages are read again; your proofing on them is undone with Ctrl+Z.");
    m_help.set_xalign(0);
    m_help.set_wrap(true);
    m_help.add_css_class("dim-label");
    m_area.set_vexpand(true);
    m_area.set_hexpand(true);
    m_area.set_draw_func([this](const Cairo::RefPtr<Cairo::Context>& cr, int, int) {
        double s, ox, oy;
        if (!layout(s, ox, oy)) return;
        cr->save();
        cr->translate(ox, oy);
        cr->scale(s / m_pic->scale, s / m_pic->scale);
        cr->set_source(m_surface, 0, 0);
        cr->paint();
        cr->restore();
        const double w = s * m_pic->src_w, h = s * m_pic->src_h;
        if (m_two) {
            // The two pages, tinted apart; the cut a strong line.
            const double x = ox + s * m_cut;
            cr->set_source_rgba(0.20, 0.45, 0.85, 0.10);
            cr->rectangle(ox, oy, x - ox, h);
            cr->fill();
            cr->set_source_rgba(0.85, 0.45, 0.20, 0.10);
            cr->rectangle(x, oy, ox + w - x, h);
            cr->fill();
            cr->set_source_rgb(0.90, 0.15, 0.15);
            cr->set_line_width(2.0);
            cr->move_to(x, oy - 6);
            cr->line_to(x, oy + h + 6);
            cr->stroke();
        }
        cr->set_source_rgba(0.5, 0.5, 0.5, 0.8);
        cr->set_line_width(1.0);
        cr->rectangle(ox + 0.5, oy + 0.5, w - 1, h - 1);
        cr->stroke();
    });
    auto click = Gtk::GestureClick::create();
    click->signal_pressed().connect([this](int, double x, double) { set_cut_from_view(x); });
    m_area.add_controller(click);
    auto drag = Gtk::GestureDrag::create();
    drag->signal_drag_update().connect([this, drag](double dx, double) {
        double x0, y0;
        if (drag->get_start_point(x0, y0)) set_cut_from_view(x0 + dx);
    });
    m_area.add_controller(drag);

    m_pair.set_group(m_one);
    m_one.signal_toggled().connect([this] { if (m_one.get_active()) { m_two = false; sync(); } });
    m_pair.signal_toggled().connect([this] { if (m_pair.get_active()) { m_two = true; sync(); } });
    m_where.set_hexpand(true);
    m_where.set_xalign(0);
    m_ok.add_css_class("suggested-action");
    m_cancel.signal_clicked().connect([this] { set_visible(false); });
    m_ok.signal_clicked().connect([this] {
        if (!m_pic) return;
        set_visible(false);
        signal_recut.emit(m_scan, core::Rect{0, 0, m_pic->src_w, m_pic->src_h}, m_two ? m_cut : 0);
    });
    m_row.append(m_one);
    m_row.append(m_pair);
    m_row.append(m_where);
    m_row.append(m_cancel);
    m_row.append(m_ok);

    m_box.set_margin(14);
    m_box.append(m_help);
    m_box.append(m_area);
    m_box.append(m_row);
    set_child(m_box);
}

void RecutWindow::open_for(const std::string& scan_id, const std::string& title, bool two, int cut) {
    if (scan_id != m_scan) { m_pic.reset(); m_surface.reset(); }
    m_scan = scan_id;
    m_two = two;
    m_cut = cut;
    set_title("Re-cut the Scan -- " + title);
    (two ? m_pair : m_one).set_active(true);
    sync();
    present();
}

void RecutWindow::set_picture(const std::string& scan_id, std::shared_ptr<const core::Picture> pic) {
    if (scan_id != m_scan || !pic || !pic->valid()) return;
    m_pic = std::move(pic);
    m_surface = Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32, m_pic->width, m_pic->height);
    m_surface->flush();
    for (int y = 0; y < m_pic->height; ++y)
        std::memcpy(m_surface->get_data() + std::size_t(y) * m_surface->get_stride(),
                    m_pic->px.data() + std::size_t(y) * m_pic->width, std::size_t(m_pic->width) * 4);
    m_surface->mark_dirty();
    if (m_cut <= 0 || m_cut >= m_pic->src_w) m_cut = m_pic->src_w / 2;
    sync();
}

void RecutWindow::poke(bool two, int cut) {
    if (!m_pic) return;
    m_two = two;
    m_cut = std::clamp(cut, 1, m_pic->src_w - 1);
    signal_recut.emit(m_scan, core::Rect{0, 0, m_pic->src_w, m_pic->src_h}, m_two ? m_cut : 0);
}

bool RecutWindow::layout(double& s, double& ox, double& oy) const {
    if (!m_pic || !m_surface) return false;
    const double vw = m_area.get_width() - 2 * kPad, vh = m_area.get_height() - 2 * kPad;
    if (vw <= 0 || vh <= 0 || m_pic->src_w <= 0 || m_pic->src_h <= 0) return false;
    s = std::min(vw / m_pic->src_w, vh / m_pic->src_h);
    ox = kPad + (vw - s * m_pic->src_w) / 2;
    oy = kPad + (vh - s * m_pic->src_h) / 2;
    return true;
}

void RecutWindow::set_cut_from_view(double x) {
    double s, ox, oy;
    if (!layout(s, ox, oy)) return;
    if (!m_two) { m_two = true; m_pair.set_active(true); }
    m_cut = std::clamp(int(std::lround((x - ox) / s)), 1, m_pic->src_w - 1);
    sync();
}

void RecutWindow::sync() {
    m_ok.set_sensitive(m_pic != nullptr);
    if (!m_pic)       m_where.set_text("Loading the scan…");
    else if (!m_two)  m_where.set_text("The whole scan, " + std::to_string(m_pic->src_w) + " px wide, as one page");
    else              m_where.set_text("Cut at " + std::to_string(m_cut) + " of " + std::to_string(m_pic->src_w) + " px");
    m_area.queue_draw();
}

}  // namespace optik
