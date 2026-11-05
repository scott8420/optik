#pragma once
#include <spdlog/spdlog.h>
#include <memory>

// Per-area logging (CANON: "Per-method logging areas with runtime toggle").
//
// Every log line belongs to an AREA named by concern, not by file. When
// something misbehaves the move is: turn the suspect area to TRACE, reproduce,
// read the trace -- which is also the two-fails-drop-to-log-mode substrate from
// RULES. Areas map to spdlog named loggers sharing one file sink.
//
// This header is UI-side. optik::model is GTK-free and (for now) logging-free;
// the UI wraps engine calls in log scope. That keeps the engine lightest and
// matches logging living with the process that owns a log file. (Whether the
// engine eventually gets its own logging is an open fork in DESIGN.)
//
// Areas mirror the DESIGN vocabulary so a new reader sees the decomposition:
// App (lifecycle), Intake/Condition/Recognize (the pipeline stages), Io
// (project read/write), Canvas/Editor (the interactive panes).
namespace optik::log {

enum class Area { App, Import, Read, Io, View, Text };

// Create the shared file sink (in the user data dir) and register one logger
// per area. Idempotent -- safe to call once at Application startup.
void init();

// The logger for an area (nullptr only if init() hasn't run yet -- callers in
// early construction paths should null-check).
std::shared_ptr<spdlog::logger> get(Area area);

// Runtime level for one area. Wire to a debug menu later; for now it's the
// programmatic hook diagnosis uses.
void set_level(Area area, spdlog::level::level_enum level);

}  // namespace optik::log
