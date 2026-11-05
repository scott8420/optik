#include "Css.hpp"

#include <gdkmm/display.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/stylecontext.h>

namespace optik::css {

void init() {
    auto display = Gdk::Display::get_default();
    if (!display) return;
    auto provider = Gtk::CssProvider::create();
    provider->load_from_data(R"CSS(
.optik-pageview { background-color: #2b2b2b; }
.optik-text, .optik-text text { font-family: serif; font-size: 13pt; }
.optik-pagerow-title { font-weight: bold; }
.optik-pagerow-status { font-size: 9pt; opacity: 0.75; }
.optik-pagerow-check { font-size: 9pt; color: #f2c14e; }
.optik-status { padding: 3px 8px; }
.optik-empty { font-size: 14pt; opacity: 0.6; }
)CSS");
    Gtk::StyleContext::add_provider_for_display(display, provider,
                                                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

}  // namespace optik::css
