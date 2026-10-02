#include "pool.h"

#include <windows.h>

#include <algorithm>
#include <charconv>

#include "storage.h"

namespace sudoku {
namespace {

constexpr int kBackgroundWorkers = 2;

// EcoQoS: lets Windows run background generation on the most power-efficient schedule.
void setEfficiencyMode(bool eco) {
    THREAD_POWER_THROTTLING_STATE s{};
    s.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
    s.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
    s.StateMask = eco ? THREAD_POWER_THROTTLING_EXECUTION_SPEED : 0;
    SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &s, sizeof(s));
    SetThreadPriority(GetCurrentThread(), eco ? THREAD_PRIORITY_BELOW_NORMAL : THREAD_PRIORITY_NORMAL);
}

}  // namespace

void PuzzlePool::start(void* notifyHwnd, unsigned notifyMsg, std::wstring file) {
    if (started_) return;
    started_ = true;
    hwnd_ = notifyHwnd;
    msg_ = notifyMsg;
    file_ = std::move(file);
    cancel_ = false;
    load();
    const int n = std::max(kBackgroundWorkers, int(std::thread::hardware_concurrency()) - 1);
    for (int i = 0; i < n; ++i)
        workers_.emplace_back([this, i](std::stop_token st) { workerLoop(st, i); });
}

void PuzzlePool::stop() {
    if (!started_) return;
    started_ = false;
    cancel_ = true;
    for (auto& w : workers_) w.request_stop();
    cv_.notify_all();
    workers_.clear();  // joins
    save();
}

bool PuzzlePool::take(Difficulty d, Puzzle& out) {
    std::lock_guard lk(mu_);
    auto& q = ready_[int(d)];
    if (q.empty()) return false;
    out = q.front();
    q.pop_front();
    cv_.notify_all();  // let a background worker refill
    return true;
}

void PuzzlePool::request(Difficulty d) {
    {
        std::lock_guard lk(mu_);
        urgent_ = int(d);
    }
    cv_.notify_all();
}

int PuzzlePool::available(Difficulty d) const {
    std::lock_guard lk(mu_);
    return int(ready_[int(d)].size());
}

void PuzzlePool::requestDaily(int date) {
    {
        std::lock_guard lk(mu_);
        if (dailies_.count(date) || std::find(dailyRequests_.begin(), dailyRequests_.end(), date) != dailyRequests_.end())
            return;
        dailyRequests_.push_back(date);
    }
    cv_.notify_all();
}

bool PuzzlePool::takeDaily(int date, Puzzle& out) {
    std::lock_guard lk(mu_);
    auto it = dailies_.find(date);
    if (it == dailies_.end()) return false;
    out = it->second;
    return true;
}

bool PuzzlePool::pickJob(int index, Job& job, Difficulty& d, int& date) {
    if (!dailyRequests_.empty()) {
        job = Job::Daily;
        date = dailyRequests_.front();
        dailyRequests_.erase(dailyRequests_.begin());
        return true;
    }
    if (urgent_ >= 0) {
        job = Job::Classic;
        d = Difficulty(urgent_);
        return true;
    }
    if (index >= kBackgroundWorkers) return false;
    int best = -1, bestFill = kTarget;
    for (int i = 0; i < kDifficultyCount; ++i) {
        const int fill = int(ready_[i].size()) + inFlight_[i];
        if (fill < bestFill) {
            bestFill = fill;
            best = i;
        }
    }
    if (best < 0) return false;
    job = Job::Classic;
    d = Difficulty(best);
    return true;
}

void PuzzlePool::workerLoop(std::stop_token st, int index) {
    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);
    Rng rng(uint64_t(qpc.QuadPart) ^ (uint64_t(GetCurrentThreadId()) << 32) ^ uint64_t(index) * 0x9E3779B97F4A7C15ull);
    while (!st.stop_requested()) {
        Job job = Job::None;
        Difficulty d = Difficulty::Easy;
        int date = 0;
        bool urgent = false;
        {
            std::unique_lock lk(mu_);
            if (!cv_.wait(lk, st, [&] { return pickJob(index, job, d, date); })) return;
            urgent = job == Job::Daily || urgent_ == int(d);
            if (job == Job::Classic) ++inFlight_[int(d)];
        }
        setEfficiencyMode(!urgent);
        if (job == Job::Daily) {
            const Puzzle p = generateDaily(date);
            {
                std::lock_guard lk(mu_);
                dailies_[date] = p;
            }
            post(1, date);
            continue;
        }
        Puzzle p;
        const bool ok = generate(d, rng, p, &cancel_);
        {
            std::lock_guard lk(mu_);
            --inFlight_[int(d)];
            if (!ok) return;  // cancelled
            ready_[int(d)].push_back(p);
            if (urgent_ == int(d)) urgent_ = -1;
        }
        post(0, int(d));
    }
}

void PuzzlePool::post(unsigned wParam, long long lParam) {
    if (hwnd_) PostMessageW(static_cast<HWND>(hwnd_), msg_, WPARAM(wParam), LPARAM(lParam));
}

// Pool file: one puzzle per line: "<difficulty> <givens> <solution>".
void PuzzlePool::load() {
    std::string text;
    if (file_.empty() || !storage::readText(file_, text)) return;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string_view line(text.data() + pos, end - pos);
        pos = end + 1;
        if (line.size() < 1 + 1 + 81 + 1 + 81) continue;
        const int d = line[0] - '0';
        if (d < 0 || d >= kDifficultyCount) continue;
        Puzzle p;
        p.difficulty = Difficulty(d);
        if (!fromString(line.substr(2, 81), p.givens) || !fromString(line.substr(84, 81), p.solution)) continue;
        if (!isCompleteSolution(p.solution)) continue;
        if (int(ready_[d].size()) < kTarget * 2) ready_[d].push_back(p);
    }
}

void PuzzlePool::save() {
    if (file_.empty()) return;
    std::string out;
    std::lock_guard lk(mu_);
    for (int d = 0; d < kDifficultyCount; ++d) {
        for (const Puzzle& p : ready_[d]) {
            out += char('0' + d);
            out += ' ';
            out += toString(p.givens);
            out += ' ';
            out += toString(p.solution);
            out += '\n';
        }
    }
    storage::writeAtomic(file_, out);
}

}  // namespace sudoku
