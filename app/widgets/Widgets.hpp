#pragma once
#include "WidgetRegistry.hpp"

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/entry.h>
#include <gtkmm/headerbar.h>
#include <gtkmm/image.h>
#include <gtkmm/linkbutton.h>
#include <gtkmm/overlay.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/revealer.h>
#include <gtkmm/paned.h>
#include <gtkmm/picture.h>
#include <gtkmm/progressbar.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/separator.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/stack.h>
#include <gtkmm/textview.h>
#include <gtkmm/togglebutton.h>
#include <gtkmm/widget.h>

#include <string>
#include <string_view>
#include <utility>

// Discipline through inheritance (CANON). A framework widget's name is optional
// and easy to skip; over hundreds of edits the Inspector tree becomes a forest
// of anonymous nodes and warnings lose their thread back to the object. The
// Named<W> wrapper makes the name mandatory in the ONLY constructor and
// registers into the address book by construction -- the wrong thing (an
// unnamed widget) simply isn't expressible in app code.
//
// One template, every wrapper as an alias, so the discipline costs almost
// nothing and new wrappers are one-line additions as demand appears (CANON:
// substrate widening follows demand, not symmetry). Custom subclasses that
// carry real logic (MainWindow, and later Canvas/Strip/Editor) name and
// register themselves directly rather than through this template.
namespace optik::widgets {

// Opt-out tag for transient per-row content (thumbnails, list rows). Such
// widgets are named for the Inspector but NOT entered in the diagnosis registry
// -- the registry is a live address book of meaningful, addressable surfaces,
// not a dumping ground for hundreds of rebuilt-on-every-reorder row internals
// (CANON: the `unregistered_t` substrate-widening primitive; first use here is
// the Strip's thumbnails). Registered widgets stay the default; opting out is
// explicit.
struct unregistered_t {};
inline constexpr unregistered_t unregistered{};

template <class W>
class Named : public W {
public:
    template <class... Args>
    explicit Named(std::string_view name, Args&&... args)
        : W(std::forward<Args>(args)...) {
        this->set_name(std::string(name));
        registry::add(name, this);
        m_registered = true;
    }

    // Parallel ctor: name for the Inspector, skip the registry.
    template <class... Args>
    explicit Named(unregistered_t, std::string_view name, Args&&... args)
        : W(std::forward<Args>(args)...) {
        this->set_name(std::string(name));
    }

    ~Named() override {
        if (m_registered) registry::remove(this);
    }

private:
    bool m_registered = false;
};

using Box            = Named<Gtk::Box>;
using Button         = Named<Gtk::Button>;
using CheckButton    = Named<Gtk::CheckButton>;
using DrawingArea    = Named<Gtk::DrawingArea>;
using Entry          = Named<Gtk::Entry>;
using HeaderBar      = Named<Gtk::HeaderBar>;
using Image          = Named<Gtk::Image>;
using LinkButton     = Named<Gtk::LinkButton>;
using Label          = Named<Gtk::Label>;
using ListBox        = Named<Gtk::ListBox>;
using MenuButton     = Named<Gtk::MenuButton>;
using Overlay        = Named<Gtk::Overlay>;
using Paned          = Named<Gtk::Paned>;
using Picture        = Named<Gtk::Picture>;
using ProgressBar    = Named<Gtk::ProgressBar>;
using Revealer       = Named<Gtk::Revealer>;
using ScrolledWindow = Named<Gtk::ScrolledWindow>;
using Separator      = Named<Gtk::Separator>;
using SpinButton     = Named<Gtk::SpinButton>;
using Stack          = Named<Gtk::Stack>;
using TextView       = Named<Gtk::TextView>;
using ToggleButton   = Named<Gtk::ToggleButton>;

}  // namespace optik::widgets
