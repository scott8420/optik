#include "Jobs.hpp"

#include <cstdlib>

namespace optik::core {

Jobs::Jobs(int workers) {
    // Tesseract's own OpenMP threads would fight the pool for every core.
    // One thread per engine; the pool is the parallelism.
    setenv("OMP_THREAD_LIMIT", "1", 0);
    if (workers <= 0) {
        const unsigned hw = std::thread::hardware_concurrency();
        workers = hw > 1 ? int(hw) - 1 : 1;
    }
    for (int i = 0; i < workers; ++i) m_threads.emplace_back([this] { loop(); });
}

Jobs::~Jobs() {
    {
        std::lock_guard<std::mutex> lk(m_mu);
        m_stop = true;
        m_queue.clear();
    }
    m_cv.notify_all();
    for (auto& t : m_threads) t.join();
}

void Jobs::submit(Work w) {
    {
        std::lock_guard<std::mutex> lk(m_mu);
        m_queue.push_back(std::move(w));
    }
    m_cv.notify_one();
}

void Jobs::cancel() {
    std::lock_guard<std::mutex> lk(m_mu);
    m_queue.clear();
    m_done.clear();
    ++m_generation;   // work already running lands into nothing
}

std::size_t Jobs::queued() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_queue.size();
}

std::size_t Jobs::waiting_to_land() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_done.size();
}

int Jobs::pump() {
    std::deque<Landing> ready;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        ready.swap(m_done);
    }
    for (auto& l : ready) if (l) l();
    return int(ready.size());
}

void Jobs::loop() {
    ocr::Engine engine;   // this worker's own, for its whole life
    for (;;) {
        Work w;
        unsigned gen = 0;
        {
            std::unique_lock<std::mutex> lk(m_mu);
            m_cv.wait(lk, [this] { return m_stop || !m_queue.empty(); });
            if (m_stop) return;
            w = std::move(m_queue.front());
            m_queue.pop_front();
            gen = m_generation.load();
            ++m_running;
        }
        Landing l = w(engine);
        bool notify = false;
        {
            std::lock_guard<std::mutex> lk(m_mu);
            --m_running;
            if (gen == m_generation.load() && !m_stop) {
                m_done.push_back(std::move(l));
                notify = true;
            }
        }
        if (notify && m_notify) m_notify();
    }
}

}  // namespace optik::core
