#pragma once
// PageView -- the page as scanned, with what the engine made of it drawn over:
// its areas (coloured by kind) and its uncertain words (amber).
//
// FineReader's image pane. The author points at things here:
//   click a word        -> that word, in the text pane too
//   click in an area    -> select the area (its kind, read again, delete)
//   drag a selected area's edge or corner -> resize it; it is read again
//   Draw tool: drag     -> a new text area, read
//
// It draws from a const Page the Shell hands it; it never writes the Book. Every
// change goes back out as a signal, and the Shell makes it a journal step.

#include "core/Book.hpp"
#include "core/Picture.hpp"
#include "widgets/Widgets.hpp"

#include <cairomm/surface.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/eventcontrollerscroll.h>
#include <gtkmm/gesturedrag.h>
#include <sigc++/signal.h>

#include <memory>
#include <optional>

namespace optik {

struct WordRef {
    int area = -1, para = -1, line = -1, word = -1;
    bool valid() const { return area >= 0 && para >= 0 && line >= 0 && word >= 0; }
    bool operator==(const WordRef&) const = default;
};

// The word a ref names in `page`, or nullptr if the ref has gone stale.
const ocr::Word* word_at(const core::Page& page, const WordRef& r);

class PageView : public widgets::DrawingArea {
public:
    enum class Tool { Select, Draw };
    // How the page fits: the whole page, the page's width (scroll down it), or
    // a free scale the author zoomed to. Kept across pages; the pan resets.
    enum class Fit { Page, Width, Free };

    PageView();

    // `page` must outlive the next call; the Shell re-shows after every change.
    void show(const core::Book* book, const core::Page* page);
    void set_picture(const std::string& look_key, std::shared_ptr<const core::Picture> pic);   // core::look_key
    const std::string& picture_key() const { return m_pic_key; }   // the sweep's witness (s29)
    void set_word(const WordRef& w);
    void set_area(int area);
    void set_tool(Tool t);
    Tool tool() const { return m_tool; }

    void   set_fit(Fit f);
    void   zoom_by(double factor);              // about the view's centre
    double zoom() const;                         // view px per page px, now
    sigc::signal<void()> signal_zoom_changed;

    // The sweep's hands (s26, app/Sweep.cpp): the very handlers the pointer
    // gestures call, in the order GTK calls them (a drag starts with a press).
    void poke_click(double x, double y) { on_press(1, x, y); }
    void poke_drag(double x0, double y0, double x1, double y1);
    bool page_to_view(double px, double py, double& x, double& y) const;   // false: no layout

    sigc::signal<void(WordRef)>          signal_word_clicked;
    sigc::signal<void(int)>              signal_area_selected;   // -1 = none
    sigc::signal<void(core::Rect)>       signal_area_drawn;
    sigc::signal<void(int, core::Rect)>  signal_area_resized;

private:
    void on_draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);
    void on_press(int n, double x, double y);
    void on_drag_begin(double x, double y);
    void on_drag_update(double dx, double dy);
    void on_drag_end(double dx, double dy);
    bool on_scroll(double dx, double dy);
    void zoom_at(double factor, double x, double y);

    // view <-> page space
    bool   layout(double& s, double& ox, double& oy) const;
    double to_page_x(double x) const;
    double to_page_y(double y) const;
    core::Rect drag_rect() const;
    int    grip_edges(double x, double y) const;   // the selected area's edges here

    const core::Book* m_book = nullptr;
    const core::Page* m_page = nullptr;
    std::string m_pic_key;                      // the look the picture was made for (s26)
    std::shared_ptr<const core::Picture> m_pic;
    Cairo::RefPtr<Cairo::ImageSurface>   m_surface;
    WordRef m_word;
    int     m_area = -1;
    Tool    m_tool = Tool::Select;

    Glib::RefPtr<Gtk::GestureClick> m_click;
    Glib::RefPtr<Gtk::GestureDrag>  m_drag;
    Glib::RefPtr<Gtk::GestureDrag>  m_pan;      // middle button
    Fit    m_fit = Fit::Page;
    double m_scale = 1.0;                       // for Fit::Free
    mutable double m_pan_x = 0, m_pan_y = 0;    // view px scrolled, clamped in layout()
    double m_pan_x0 = 0, m_pan_y0 = 0;
    Glib::RefPtr<Gtk::EventController> m_scroll;
    double m_px = 0, m_py = 0;
    bool   m_pointer_in = false;
    // A drag in progress: drawing a new area, or which edges of the selected
    // area it moves (bit 1 left, 2 right, 4 top, 8 bottom).
    bool   m_dragging = false;
    int    m_edges = 0;
    double m_x0 = 0, m_y0 = 0, m_dx = 0, m_dy = 0;
};

}  // namespace optik
