#pragma once
#include <gtkmm/application.h>
#include <memory>

namespace optik {

class Shell;

class App : public Gtk::Application {
public:
    static Glib::RefPtr<App> create();
    ~App() override;

protected:
    App();
    void on_activate() override;
    void on_open(const type_vec_files& files, const Glib::ustring& hint) override;

private:
    Shell& shell();
    std::unique_ptr<Shell> m_shell;
};

}  // namespace optik
