// Small synthesized sound effects, generated once into in-memory WAV buffers.
#pragma once

#include <cstdint>
#include <vector>

namespace sudoku {

enum class Sfx : uint8_t { Place, Note, Erase, Error, Unit, Win, Achievement, Count };

class SoundPlayer {
public:
    void configure(bool enabled, int volume);  // volume 1..3
    void play(Sfx s);

private:
    void build();
    bool enabled_ = false;
    int volume_ = 2;
    int builtVolume_ = 0;
    std::vector<uint8_t> wav_[int(Sfx::Count)];
};

}  // namespace sudoku
