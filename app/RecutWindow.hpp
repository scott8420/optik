#pragma once
// RecutWindow -- "Re-cut the Scan" (s26, milestone 12). The whole scan the
// current page came from, with the cut drawn on it: one page, or two with the
// gutter where the author drags it. Re-cut hands (scan, whole scan, cut) back
// to the Shell, which makes it one journal step and reads the pages again.
//
// Made once by the Shell, hidden on close (CANON: born-modern dialogs). Modal
// to the window -- a choice, not a wait: the main loop runs, reads land.
#include "core/Book.hpp"
#include "core/Picture.hpp"
#include "widgets/Widgets.hpp"

#include <cairomm/surface.h>
#include <gtkmm/window.h>
#include <sigc++/signal.h>

#include <memory>

namespace optik {

class RecutWindow : public Gtk::Window {
public:
    RecutWindow();

    // Open for `scan_id`; `two` and `cut` (scan px) are what the scan has now.
    // The picture arrives later (set_picture); Re-cut waits for it.
    void open_for(const std::string& scan_id, const std::string& title, bool two, int cut);
    void set_picture(const std::string& scan_id, std::shared_ptr<const core::Picture> pic);

    // scan id, the whole scan (0, 0, width, height), cut x or 0 for one page
    sigc::signal<void(std::string, core::Rect, int)> signal_recut;

    // The sweep's hands (s26): what Re-cut does, with these choices.
    void poke(bool two, int cut);

private:
    void sync();
    bool layout(double& s, double& ox, double& oy) const;
    void set_cut_from_view(double x);

    std::string m_scan;
    std::shared_ptr<const core::Picture> m_pic;
    Cairo::RefPtr<Cairo::ImageSurface>   m_surface;
    bool m_two = true;
    int  m_cut = 0;

    widgets::Box          m_box{"recut.box", Gtk::Orientation::VERTICAL, 10};
    widgets::Label        m_help{"recut.help"};
    widgets::DrawingArea  m_area{"recut.scan"};
    widgets::Box          m_row{"recut.row", Gtk::Orientation::HORIZONTAL, 8};
    widgets::ToggleButton m_one{"recut.one", "One page"};
    widgets::ToggleButton m_pair{"recut.two", "Two pages"};
    widgets::Label        m_where{"recut.where"};
    widgets::Button       m_cancel{"recut.cancel", "Cancel"};
    widgets::Button       m_ok{"recut.ok", "Re-cut"};
};

}  // namespace optik
