// Sweep.cpp -- the app driving itself (s26, milestone 11; Klimt's sweep, here).
//
//   OPTIK_SWEEP=1 optik <book or scans folder>        (scripts/sweep.sh runs it
//                                                       under Xvfb and ASan)
//
// Waits for the book to be open and read, then walks it as an author would and
// worse: every menu verb on the first pages (through activate_action, so only
// what the menu would let a person reach), then the canvas -- clicks on a grid,
// every area's edges dragged in and out, areas drawn forwards, backwards, too
// small and off the page, at two zooms. After EVERY step, once the pool is
// quiet: core::audit on the book, and the window's own invariants (the pane
// has a row per page, the selected area exists, the shown page exists).
// Then undo to where it started (the book must equal the start), redo to the
// end (must equal the end), save, open the file again (must equal the book).
//
// Prints "sweep ok/FAIL <step>" lines and a summary; the process exits 0 only
// when nothing failed (main.cpp asks sweep_status()). OPTIK_SWEEP_SHOTS=<dir>
// also writes the window as PNGs at a few points -- the trace's own pictures.
//
// The canvas is poked through PageView's handlers (poke_click / poke_drag),
// not synthesised GDK events: GTK 4 has no public way to inject pointer input.
// What is tested is everything from the handler down; the gesture wiring above
// it is the part the walk in docs/regression.md still covers by hand.
#include "Shell.hpp"

#include "Log.hpp"
#include "RecutWindow.hpp"
#include "core/Audit.hpp"
#include "core/PageOps.hpp"

#include <glibmm/main.h>
#include <gtk/gtk.h>

#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace optik {

namespace {
int g_sweep_status = -1;   // -1: no sweep ran
}
int sweep_status() { return g_sweep_status; }

struct Shell::Sweep {
    struct Step {
        std::string name;
        std::function<void()> act;
    };
    std::deque<Step> todo;
    std::string running;                       // the step whose effects we wait on
    bool started = false;                      // the book is open and read
    int  steps = 0, fails = 0;
    std::chrono::steady_clock::time_point since = std::chrono::steady_clock::now();
    core::Book start, end;
    std::size_t start_pos = 0, end_pos = 0;
    std::string shots;                         // OPTIK_SWEEP_SHOTS
    std::string race_page;                     // the race steps' witness
    std::vector<ocr::Area> race_rest;
    sigc::connection beat;

    void add(std::string name, std::function<void()> fn) { todo.push_back({std::move(name), std::move(fn)}); }
    // Inserted to run next, in the order given (a planning step adds its children).
    void next(std::vector<Step> v) { todo.insert(todo.begin(), v.begin(), v.end()); }
};

void Shell::start_sweep() {
    m_sweep = std::make_shared<Sweep>();
    if (const char* d = std::getenv("OPTIK_SWEEP_SHOTS")) m_sweep->shots = d;
    std::cout << "sweep: waiting for the book to be open and read" << std::endl;
    m_sweep->beat = Glib::signal_timeout().connect(sigc::mem_fun(*this, &Shell::sweep_tick), 25);
}

bool Shell::sweep_settled() const {
    return m_jobs.idle() && m_batch_total == 0 && m_pictures_pending.empty() && m_thumbs_pending.empty();
}

void Shell::sweep_check(const std::string& what) {
    auto& S = *m_sweep;
    ++S.steps;
    std::vector<std::string> bad = core::audit(m_book);
    if (m_pages.rows() != m_book.pages().size())
        bad.push_back("pages pane has " + std::to_string(m_pages.rows()) + " rows for " +
                      std::to_string(m_book.pages().size()) + " pages");
    if (m_book.pages().empty() != m_page_id.empty())
        bad.push_back("shown page '" + m_page_id + "' vs " + std::to_string(m_book.pages().size()) + " pages");
    if (!m_page_id.empty() && !current()) bad.push_back("shown page " + m_page_id + " is not in the book");
    if (const core::Page* p = current(); p && (m_area < -1 || m_area >= int(p->areas.size())))
        bad.push_back("selected area " + std::to_string(m_area) + " of " + std::to_string(p->areas.size()));
    if (m_word.valid() && current() && !word_at(*current(), m_word) && m_actions["accept"]->get_enabled())
        bad.push_back("Accept is enabled on a word that has gone");
    if (bad.empty()) {
        std::cout << "sweep ok   " << what << "\n";
    } else {
        ++S.fails;
        std::cout << "sweep FAIL " << what << ": " << bad[0];
        if (bad.size() > 1) std::cout << "  (+" << bad.size() - 1 << " more)";
        std::cout << "\n";
        if (auto lg = log::get(log::Area::App)) lg->error("sweep FAIL {}: {}", what, bad[0]);
    }
    std::cout.flush();
}

bool Shell::sweep_tick() {
    auto& S = *m_sweep;
    using clock = std::chrono::steady_clock;
    if (!S.started) {
        const bool read = m_has_book && !m_book.pages().empty() && [&] {
            for (const auto& p : m_book.pages()) if (!p.read) return false;
            return true;
        }();
        if (!(read && sweep_settled())) {
            if (clock::now() - S.since > std::chrono::minutes(10)) {
                std::cout << "sweep FAIL the book never finished opening and reading\n";
                S.fails = 1;
                sweep_finish();
                return false;
            }
            return true;
        }
        S.started = true;
        S.start = m_book;
        S.start_pos = m_journal.position();
        std::cout << "sweep: " << m_book.pages().size() << " pages read; journal at "
                  << S.start_pos << std::endl;
        sweep_check("the book as opened");
        sweep_shot("0_opened");

        const int n = std::min<int>(3, int(m_book.pages().size()));
        for (int i = 0; i < n; ++i) S.add("plan: verbs on page " + std::to_string(i + 1), [this, i] { sweep_plan_page(i); });
        S.add("plan: the canvas, page 1", [this] { go_to(0); sweep_plan_canvas(); });
        if (m_book.pages().size() > 1)
            S.add("plan: the canvas, page 2", [this] { go_to(1); sweep_plan_canvas(); });
        S.add("turn page 1 clockwise", [this] { go_to(0); activate_action("win.rotate-right"); });
        S.add("plan: the canvas, page 1 turned", [this] { go_to(0); sweep_plan_canvas(); });
        S.add("plan: page operations", [this] { sweep_plan_pages(); });
        S.add("Rescan, cancelled at once", [this] { activate_action("win.rescan"); activate_action("win.cancel"); });
        S.add("Rescan Book", [this] { activate_action("win.rescan"); });
        S.add("Read All", [this] { activate_action("win.read-all"); });
        S.add("F4 round the whole book", [this] {
            for (int k = 0; k < 400; ++k) activate_action("win.next");
        });
        S.add("Undo to the start", [this] {
            auto& S = *m_sweep;
            S.end = m_book;
            S.end_pos = m_journal.position();
            while (m_journal.position() > S.start_pos && m_journal.can_undo()) undo();
            std::string why;
            if (!core::same_book(m_book, S.start, &why)) {
                ++S.fails;
                std::cout << "sweep FAIL undo to the start is not the start: " << why << "\n";
            }
        });
        S.add("Redo to the end", [this] {
            auto& S = *m_sweep;
            while (m_journal.position() < S.end_pos && m_journal.can_redo()) redo();
            std::string why;
            if (!core::same_book(m_book, S.end, &why)) {
                ++S.fails;
                std::cout << "sweep FAIL redo to the end is not the end: " << why << "\n";
            }
        });
        S.add("Save, open again", [this] {
            auto& S = *m_sweep;
            activate_action("win.save");
            core::Book again;
            std::string why;
            if (core::Book::open(m_book.folder(), again) != core::Status::Ok) {
                ++S.fails;
                std::cout << "sweep FAIL the saved book does not open\n";
            } else if (!core::same_book(m_book, again, &why)) {
                ++S.fails;
                std::cout << "sweep FAIL what was saved is not what opened: " << why << "\n";
            }
        });
        return true;
    }

    // A step is running: wait for its jobs to land, bounded.
    if (!S.running.empty()) {
        if (!sweep_settled()) {
            if (clock::now() - S.since > std::chrono::minutes(3)) {
                ++S.fails;
                std::cout << "sweep FAIL " << S.running << ": still busy after 3 minutes\n";
                on_cancel();
            } else {
                return true;
            }
        }
        sweep_check(S.running);
        S.running.clear();
    }
    if (S.todo.empty()) {
        sweep_shot("9_end");
        sweep_finish();
        return false;
    }
    Sweep::Step s = std::move(S.todo.front());
    S.todo.pop_front();
    S.since = clock::now();
    s.act();
    S.running = s.name;
    return true;
}

void Shell::sweep_plan_page(int index) {
    auto& S = *m_sweep;
    const std::string pg = "page " + std::to_string(index + 1) + ": ";
    auto act = [this](const char* name) { return [this, name] { activate_action(name); }; };
    std::vector<Sweep::Step> v;
    v.push_back({pg + "go to", [this, index] { go_to(index); }});
    for (const char* a : {"win.next", "win.accept", "win.next", "win.accept", "win.undo", "win.redo",
                          "win.page-next", "win.page-prev", "win.fit-width", "win.zoom-in", "win.zoom-in",
                          "win.zoom-out", "win.fit-page", "win.draw", "win.draw", "win.read-page",
                          "win.undo", "win.redo", "win.save", "win.cancel"})
        v.push_back({pg + a, act(a)});
    v.push_back({pg + "Book Settings, opened and closed", [this] {
        activate_action("win.book-settings");
        m_settings_pop.popdown();
    }});
    v.push_back({pg + "About, opened and closed", [this] {
        activate_action("win.about");
        if (m_about) m_about->set_visible(false);
    }});
    // The area verbs need an area: select the first by clicking its middle.
    v.push_back({pg + "select an area by clicking it", [this] {
        const core::Page* p = current();
        double x, y;
        if (p && !p->areas.empty()) {
            const auto& b = p->areas[0].box;
            if (m_view.page_to_view((b.x0 + b.x1) / 2.0, (b.y0 + b.y1) / 2.0, x, y)) m_view.poke_click(x, y);
        }
    }});
    for (const char* k : {"heading", "picture", "ignore", "header", "footer", "text"})
        v.push_back({pg + "area kind " + k, [this, k] {
            activate_action("win.area-kind", Glib::Variant<Glib::ustring>::create(k));
        }});
    for (const char* a : {"win.area-read", "win.area-delete", "win.undo", "win.area-delete",
                          "win.area-delete", "win.undo", "win.undo", "win.redo"})
        v.push_back({pg + a, act(a)});
    S.next(std::move(v));
}

void Shell::sweep_plan_canvas() {
    auto& S = *m_sweep;
    const std::string pg = "canvas page " + std::to_string(m_book.index_of(m_page_id) + 1) + ": ";
    std::vector<Sweep::Step> v;
    const double W = m_view.get_width(), H = m_view.get_height();

    for (double zoom : {1.0, 2.5}) {
        const std::string z = zoom > 1 ? "zoomed, " : "";
        v.push_back({pg + z + "fit", [this, zoom] {
            m_view.set_fit(PageView::Fit::Page);
            if (zoom > 1) m_view.zoom_by(zoom);
        }});
        // A grid of clicks over the whole widget, margins and all.
        v.push_back({pg + z + "clicks on a 9 x 9 grid", [this, W, H] {
            for (int i = 0; i < 9; ++i)
                for (int j = 0; j < 9; ++j) m_view.poke_click(W * (i + 0.5) / 9, H * (j + 0.5) / 9);
        }});
    }
    v.push_back({pg + "fit again", [this] { m_view.set_fit(PageView::Fit::Page); }});

    // Every area's edges and a corner, dragged out and in (each one reads).
    const core::Page* p = current();
    const int areas = p ? std::min<int>(4, int(p->areas.size())) : 0;
    for (int a = 0; a < areas; ++a) {
        struct Grip { const char* name; double fx, fy; };
        for (Grip g : {Grip{"left", 0, .5}, Grip{"right", 1, .5}, Grip{"top", .5, 0},
                       Grip{"bottom", .5, 1}, Grip{"corner", 1, 1}})
            for (double d : {24.0, -12.0})
                v.push_back({pg + "area " + std::to_string(a) + " " + g.name + (d > 0 ? " out" : " in"),
                             [this, a, g, d] {
                    const core::Page* p = current();
                    if (!p || a >= int(p->areas.size())) return;
                    const auto& b = p->areas[std::size_t(a)].box;
                    double cx, cy, x, y;
                    if (!m_view.page_to_view((b.x0 + b.x1) / 2.0, (b.y0 + b.y1) / 2.0, cx, cy)) return;
                    m_view.poke_click(cx, cy);   // select it
                    if (!m_view.page_to_view(b.x0 + g.fx * b.width(), b.y0 + g.fy * b.height(), x, y)) return;
                    const double sx = g.fx == .5 ? 0 : (g.fx > .5 ? 1 : -1);
                    const double sy = g.fy == .5 ? 0 : (g.fy > .5 ? 1 : -1);
                    m_view.poke_drag(x, y, x + sx * d, y + sy * d);
                }});
    }

    // The Draw tool: forwards, backwards, too small, off the widget.
    struct Draw { const char* name; double x0, y0, x1, y1; };
    for (Draw d : {Draw{"forwards", .3, .3, .6, .45}, Draw{"backwards", .7, .8, .4, .6},
                   Draw{"too small", .5, .5, .502, .503}, Draw{"off the page", .9, .9, 1.4, 1.3},
                   Draw{"from outside in", -.2, -.2, .25, .2}})
        v.push_back({pg + "draw " + d.name, [this, d, W, H] {
            set_draw(true);
            m_view.poke_drag(W * d.x0, H * d.y0, W * d.x1, H * d.y1);
            set_draw(false);
        }});
    // Races: the read a drag starts is still on a worker when the area list
    // changes under it. Its landing must not write into the wrong area.
    v.push_back({pg + "race: resize an area, delete it before the read lands", [this] {
        const core::Page* p = current();
        double x, y, cx, cy;
        if (!p || p->areas.size() < 2) { std::cout << "sweep: race skipped, fewer than 2 areas\n"; return; }
        const auto b = p->areas[0].box;
        if (!m_view.page_to_view((b.x0 + b.x1) / 2.0, (b.y0 + b.y1) / 2.0, cx, cy)) { std::cout << "sweep: race skipped, no layout\n"; return; }
        m_view.poke_click(cx, cy);
        if (m_area < 0 || !m_view.page_to_view(b.x1, (b.y0 + b.y1) / 2.0, x, y)) { std::cout << "sweep: race skipped, area " << m_area << "\n"; return; }
        m_view.poke_drag(x, y, x + 20, y);   // its press may pick another area: fine
        const int gone = m_area;
        if (gone < 0) { std::cout << "sweep: race skipped, the drag left no area selected\n"; return; }
        auto rest = current()->areas;
        rest.erase(rest.begin() + gone);
        activate_action("win.area-delete");
        m_sweep->race_rest = rest;           // every other area, as it must stay
        m_sweep->race_page = m_page_id;
        std::cout << "sweep: race deletes area " << gone << " of " << rest.size() + 1 << "\n";
    }});
    v.push_back({pg + "race: the other areas are untouched", [this] {
        auto& S = *m_sweep;
        const core::Page* p = m_book.page(S.race_page);
        if (S.race_page.empty() || !p) return;
        if (p->areas != S.race_rest) {
            ++S.fails;
            std::cout << "sweep FAIL race: a read landed in another area after its own was deleted\n";
        }
        S.race_page.clear();
    }});
    v.push_back({pg + "race: draw, undo before the read lands", [this, W, H] {
        set_draw(true);
        m_view.poke_drag(W * .35, H * .35, W * .6, H * .5);
        set_draw(false);
        activate_action("win.undo");
    }});
    v.push_back({pg + "race: read the page, undo and redo before it lands", [this] {
        activate_action("win.read-page");
        activate_action("win.undo");
        activate_action("win.redo");
    }});
    v.push_back({pg + "Delete with nothing selected", [this] { m_area = -1; activate_action("win.area-delete"); }});
    v.push_back({pg + "picture", [this] {
        const core::Page* p = current();
        sweep_shot("1_" + m_page_id + (p && p->turns ? "_turned" : ""));
    }});
    S.next(std::move(v));
}

void Shell::sweep_plan_pages() {
    auto& S = *m_sweep;
    auto act = [this](const char* name) { return [this, name] { activate_action(name); }; };
    std::vector<Sweep::Step> v;
    for (int at : {0, int(m_book.pages().size()) - 1}) {
        const std::string pg = "pages, at page " + std::to_string(at + 1) + ": ";
        v.push_back({pg + "go to", [this, at] { go_to(at); }});
        for (const char* a : {"win.page-up", "win.page-down", "win.page-down", "win.page-up",
                              "win.rotate-right", "win.rotate-right", "win.rotate-left", "win.undo",
                              "win.rotate-left", "win.rotate-left", "win.page-delete", "win.undo",
                              "win.page-delete", "win.redo", "win.undo"})
            v.push_back({pg + a, act(a)});
    }
    // Re-cut, through the window (open it, let the scan load, then its button's verb).
    v.push_back({"recut: open the window on page 1", [this] { go_to(0); activate_action("win.recut"); }});
    for (double f : {0.0, 0.4, 0.5, 0.62, 0.0})
        v.push_back({std::string("recut: ") + (f > 0 ? "two pages at " + std::to_string(int(f * 100)) + "%" : "one page"),
                     [this, f] {
            if (!m_recut || !m_recut->get_visible()) {
                ++m_sweep->fails;
                std::cout << "sweep FAIL recut: the window is not open\n";
                return;
            }
            const core::Page* p = current();
            if (!p) return;
            const auto ids = core::pages_of_scan(m_book, p->scan_id);
            int w = 0;   // the scan's width: the right edge of its pages
            for (const auto& id : ids) w = std::max(w, m_book.page(id)->crop.x1);
            m_recut->poke(f > 0, int(w * f));
            m_recut->present();          // Re-cut hides it; the next choice reopens it
            std::cout << "sweep: the scan is now";
            for (const auto& id : core::pages_of_scan(m_book, current()->scan_id))
                std::cout << " " << id << "[" << m_book.page(id)->crop.x0 << "," << m_book.page(id)->crop.x1 << ")";
            std::cout << "\n";
        }});
    v.push_back({"recut: close the window", [this] { if (m_recut) m_recut->set_visible(false); }});
    for (int k = 0; k < 3; ++k) v.push_back({"recut: undo", act("win.undo")});
    // Races: the read a turn or a re-cut starts, overtaken.
    v.push_back({"race: turn, turn back before the read lands", [this] {
        go_to(0);
        activate_action("win.rotate-right");
        activate_action("win.rotate-left");
    }});
    v.push_back({"race: read the page, turn it before the read lands", [this] {
        go_to(0);
        activate_action("win.read-page");
        activate_action("win.rotate-right");
    }});
    v.push_back({"race: re-cut then undo before the reads land", [this] {
        const core::Page* p = current();
        if (!p) return;
        const auto ids = core::pages_of_scan(m_book, p->scan_id);
        int w = 0, h = 0;
        for (const auto& id : ids) {
            w = std::max(w, m_book.page(id)->crop.x1);
            h = std::max(h, m_book.page(id)->crop.y1);
        }
        recut_scan(p->scan_id, core::Rect{0, 0, w, h}, w / 3);
        activate_action("win.undo");
    }});
    S.next(std::move(v));
}

// The window as the compositor would show it, to a PNG. GTK's C API: gtkmm 4.10
// does not wrap Gsk::Renderer::render_texture.
void Shell::sweep_shot(const std::string& name) {
    auto& S = *m_sweep;
    if (S.shots.empty()) return;
    std::error_code ec;
    fs::create_directories(S.shots, ec);
    GtkWidget* w = GTK_WIDGET(gobj());
    const int width = gtk_widget_get_width(w), height = gtk_widget_get_height(w);
    GtkNative* native = gtk_widget_get_native(w);
    GskRenderer* r = native ? gtk_native_get_renderer(native) : nullptr;
    if (!r || width <= 0 || height <= 0) return;
    GdkPaintable* paint = gtk_widget_paintable_new(w);
    GtkSnapshot* snap = gtk_snapshot_new();
    gdk_paintable_snapshot(paint, GDK_SNAPSHOT(snap), width, height);
    GskRenderNode* node = gtk_snapshot_free_to_node(snap);
    if (node) {
        graphene_rect_t vp = GRAPHENE_RECT_INIT(0, 0, float(width), float(height));
        GdkTexture* tex = gsk_renderer_render_texture(r, node, &vp);
        const std::string path = S.shots + "/" + name + ".png";
        if (tex) {
            gdk_texture_save_to_png(tex, path.c_str());
            g_object_unref(tex);
            std::cout << "sweep: picture " << path << "\n";
        }
        gsk_render_node_unref(node);
    }
    g_object_unref(paint);
}

void Shell::sweep_finish() {
    auto& S = *m_sweep;
    std::cout << "---------------------------------\nsweep: " << S.steps << " steps checked, "
              << S.fails << (S.fails == 1 ? " failure" : " failures") << std::endl;
    g_sweep_status = S.fails ? 1 : 0;
    Glib::signal_idle().connect_once([this] { close(); });
}

}  // namespace optik
