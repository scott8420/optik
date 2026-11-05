#include "App.hpp"

#include "Css.hpp"
#include "Log.hpp"
#include "Shell.hpp"

#include <gdkmm/display.h>
#include <gtkmm/icontheme.h>
#include <gtkmm/settings.h>

namespace optik {

Glib::RefPtr<App> App::create() {
    return Glib::make_refptr_for_instance<App>(new App());
}

App::App() : Gtk::Application(OPTIK_APP_ID, Gio::Application::Flags::HANDLES_OPEN) {
    log::init();
    if (auto lg = log::get(log::Area::App)) lg->info("Optik {} starting", OPTIK_BUILD);
}

App::~App() = default;

Shell& App::shell() {
    if (!m_shell) {
        if (auto s = Gtk::Settings::get_default())
            s->property_gtk_application_prefer_dark_theme() = true;
        css::init();
        if (auto d = Gdk::Display::get_default())
            Gtk::IconTheme::get_for_display(d)->add_resource_path("/io/github/example/Optik/icons");
        m_shell = std::make_unique<Shell>(*this);
        add_window(*m_shell);
    }
    return *m_shell;
}

void App::on_activate() { shell().present(); }

void App::on_open(const type_vec_files& files, const Glib::ustring&) {
    Shell& s = shell();
    s.present();
    if (!files.empty()) s.open_path(files[0]->get_path());
}

}  // namespace optik
