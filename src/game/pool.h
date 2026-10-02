// Background puzzle generation. Keeps a few ready puzzles per difficulty so "New game" is
// instant. Two low-power (EcoQoS) workers refill the pool; an urgent request switches every
// core to the requested difficulty. Results are announced with PostMessage.
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "engine/generator.h"

namespace sudoku {

class PuzzlePool {
public:
    static constexpr int kTarget = 4;  // ready puzzles kept per difficulty

    // Message posted to the window: wParam 0 => lParam = difficulty ready,
    //                                wParam 1 => lParam = daily date ready.
    void start(void* notifyHwnd, unsigned notifyMsg, std::wstring file);
    void stop();  // joins the workers and persists the pool
    ~PuzzlePool() { stop(); }

    bool take(Difficulty d, Puzzle& out);  // non-blocking
    void request(Difficulty d);            // urgent: all cores until one is ready
    int available(Difficulty d) const;

    void requestDaily(int date);
    bool takeDaily(int date, Puzzle& out);

private:
    enum class Job { None, Classic, Daily };
    bool pickJob(int index, Job& job, Difficulty& d, int& date);
    void workerLoop(std::stop_token st, int index);
    void post(unsigned wParam, long long lParam);
    void load();
    void save();

    mutable std::mutex mu_;
    std::condition_variable_any cv_;
    std::deque<Puzzle> ready_[kDifficultyCount];
    int inFlight_[kDifficultyCount]{};
    int urgent_ = -1;
    std::vector<int> dailyRequests_;
    std::map<int, Puzzle> dailies_;
    std::vector<std::jthread> workers_;
    std::atomic<bool> cancel_{false};
    void* hwnd_ = nullptr;
    unsigned msg_ = 0;
    std::wstring file_;
    bool started_ = false;
};

}  // namespace sudoku
