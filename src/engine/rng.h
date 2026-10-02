// Small, fast, fully deterministic PRNG (xoshiro256** seeded through splitmix64).
// Deliberately avoids <random> distributions so daily puzzles are identical on every build.
#pragma once

#include <cstdint>
#include <utility>

namespace sudoku {

constexpr uint64_t splitmix64(uint64_t& x) {
    uint64_t z = (x += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

class Rng {
public:
    explicit Rng(uint64_t seed = 0x5EED5EED5EEDull) { reseed(seed); }

    void reseed(uint64_t seed) {
        for (auto& v : s_) v = splitmix64(seed);
    }

    uint64_t next() {
        const uint64_t result = rotl(s_[1] * 5, 7) * 9;
        const uint64_t t = s_[1] << 17;
        s_[2] ^= s_[0];
        s_[3] ^= s_[1];
        s_[1] ^= s_[2];
        s_[0] ^= s_[3];
        s_[2] ^= t;
        s_[3] = rotl(s_[3], 45);
        return result;
    }

    // Uniform-ish integer in [0, n) (multiply-shift; bias is negligible for small n).
    uint32_t below(uint32_t n) { return uint32_t((uint64_t(uint32_t(next() >> 32)) * n) >> 32); }

    // Uniform integer in [lo, hi].
    int range(int lo, int hi) { return lo + int(below(uint32_t(hi - lo + 1))); }

    template <class T>
    void shuffle(T* a, int n) {
        for (int i = n - 1; i > 0; --i) std::swap(a[i], a[below(uint32_t(i + 1))]);
    }

private:
    static constexpr uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    uint64_t s_[4];
};

}  // namespace sudoku
