#pragma once
#include <string>
#include <string_view>

namespace Gtk { class Widget; }

// The widget-naming seam (CANON: "The substrate registry is a live address
// book"). Every optik:: wrapper sets its GTK name at construction and registers
// here; the registry is a live map of name -> widget that mirrors what exists
// right now, not a survey of the source.
//
// Light half only -- diagnosis, not scripting. When GTK emits a cosmetic
// warning against an anonymous node, the name is the thread back to the object:
// find(name) in a gdb session turns a bare 0x... into a real target. (The
// scriptable-DSL half is deliberately not built; see the light-substrate fork
// carried over from the sudoku lineage.)
namespace optik::registry {

void add(std::string_view name, Gtk::Widget* w);  // warns on duplicate name
void remove(Gtk::Widget* w);
Gtk::Widget* find(std::string_view name);          // nullptr if absent
void dump();                                        // log every name -> widget

}  // namespace optik::registry
