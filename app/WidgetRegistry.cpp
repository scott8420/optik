#include "WidgetRegistry.hpp"
#include "Log.hpp"

#include <gtkmm/widget.h>

#include <string>
#include <unordered_map>

namespace optik::registry {

namespace {

// name -> widget. Live: entries appear at wrapper construction and vanish at
// destruction. Not a survey of the source; a mirror of what exists right now.
std::unordered_map<std::string, Gtk::Widget*>& table() {
    static std::unordered_map<std::string, Gtk::Widget*> t;
    return t;
}

}  // namespace

void add(std::string_view name, Gtk::Widget* w) {
    auto key = std::string(name);
    auto& t = table();
    auto it = t.find(key);
    if (it != t.end() && it->second != w) {
        // Two live widgets can't share an address. This is the discipline that
        // catches the s191-class self-rebuild collision (CANON): if it fires,
        // someone rebuilt a subtree without pre-clear unregistration.
        if (auto lg = log::get(log::Area::App))
            lg->warn("registry: duplicate widget name '{}'", key);
    }
    t[key] = w;
}

void remove(Gtk::Widget* w) {
    auto& t = table();
    for (auto it = t.begin(); it != t.end(); ++it) {
        if (it->second == w) { t.erase(it); return; }
    }
}

Gtk::Widget* find(std::string_view name) {
    auto& t = table();
    auto it = t.find(std::string(name));
    return it == t.end() ? nullptr : it->second;
}

void dump() {
    auto lg = log::get(log::Area::App);
    if (!lg) return;
    lg->info("registry: {} live widgets", table().size());
    for (const auto& [name, w] : table())
        lg->info("  {} -> {}", name, static_cast<const void*>(w));
}

}  // namespace optik::registry
