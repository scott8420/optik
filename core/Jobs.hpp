#pragma once
// core/Jobs.hpp -- the worker pool. The UI never waits (RULES).
//
// A job is work done off the UI thread that RETURNS its landing: a closure the
// UI thread runs later via pump(). Work never touches the Book; landings do,
// and they run on the thread that owns it. The app wires notify() to a
// Glib::Dispatcher so pump() runs as soon as something is ready; the selftest
// simply calls pump() in a loop.
//
// Each worker owns one OCR engine (a TessBaseAPI is not shareable), created on
// first use and kept, so a 300-page read pays for the language load once per
// worker, not per page.
//
// cancel() drops everything still queued and ignores the landings of work
// already running: "a cancel leaves nothing".

#include "ocr/Ocr.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace optik::core {

class Jobs {
public:
    using Landing = std::function<void()>;
    using Work    = std::function<Landing(ocr::Engine&)>;

    explicit Jobs(int workers = 0);     // 0 = hardware threads - 1, at least 1
    ~Jobs();
    Jobs(const Jobs&) = delete;
    Jobs& operator=(const Jobs&) = delete;

    void submit(Work w);
    void cancel();
    int  pump();                         // run ready landings here; returns how many

    std::size_t queued() const;          // waiting to start
    std::size_t running() const { return m_running.load(); }
    std::size_t waiting_to_land() const;
    bool        idle() const { return queued() == 0 && running() == 0 && waiting_to_land() == 0; }
    int         workers() const { return int(m_threads.size()); }

    // Called FROM A WORKER THREAD when a landing is ready. Must be thread-safe
    // (Glib::Dispatcher::emit is).
    void set_notify(std::function<void()> fn) { m_notify = std::move(fn); }

private:
    void loop();

    mutable std::mutex       m_mu;
    std::condition_variable  m_cv;
    std::deque<Work>         m_queue;
    std::deque<Landing>      m_done;
    std::vector<std::thread> m_threads;
    std::atomic<std::size_t> m_running{0};
    std::atomic<unsigned>    m_generation{0};
    bool                     m_stop = false;
    std::function<void()>    m_notify;
};

}  // namespace optik::core
