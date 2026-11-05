#include "Log.hpp"

#include <spdlog/sinks/basic_file_sink.h>
#include <glibmm/miscutils.h>

#include <array>
#include <filesystem>

namespace optik::log {

namespace {

const char* area_name(Area a) {
    switch (a) {
        case Area::App:    return "app";
        case Area::Import: return "import";
        case Area::Read:   return "read";
        case Area::Io:     return "io";
        case Area::View:   return "view";
        case Area::Text:   return "text";
    }
    return "app";
}

constexpr std::array<Area, 7> kAreas = {
    Area::App, Area::Import, Area::Read, Area::Io, Area::View, Area::Text};

bool g_ready = false;

}  // namespace

void init() {
    if (g_ready) return;
    try {
        namespace fs = std::filesystem;
        fs::path dir = fs::path(Glib::get_user_data_dir()) / "optik";
        fs::create_directories(dir);
        fs::path path = dir / "optik.log";

        // One sink, many loggers -- each area is an independently-levelable view
        // onto the same file. truncate=true so each run starts clean.
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
        for (Area a : kAreas) {
            auto logger = std::make_shared<spdlog::logger>(area_name(a), sink);
            logger->set_level(spdlog::level::info);
            logger->flush_on(spdlog::level::info);
            spdlog::register_logger(logger);
        }
        g_ready = true;
    } catch (...) {
        // Logging must never take the app down; if the file can't be opened we
        // run without it. get() returns null and callers no-op.
    }
}

std::shared_ptr<spdlog::logger> get(Area area) {
    return spdlog::get(area_name(area));
}

void set_level(Area area, spdlog::level::level_enum level) {
    if (auto logger = get(area)) logger->set_level(level);
}

}  // namespace optik::log
