#include "sound.h"

#include <windows.h>
#include <mmsystem.h>

#include <cmath>
#include <cstring>

namespace sudoku {
namespace {

constexpr int kRate = 44100;
constexpr float kTau = 6.2831853f;

struct Tone {
    float startSec, freq, durSec, amp, decay;  // exponential decay rate
    float sweep = 0;                           // Hz per second
};

std::vector<uint8_t> render(const Tone* tones, int count, float totalSec, float volume) {
    const int n = int(totalSec * kRate);
    std::vector<float> mixbuf(size_t(n), 0.0f);
    for (int t = 0; t < count; ++t) {
        const Tone& tone = tones[t];
        const int start = int(tone.startSec * kRate), len = int(tone.durSec * kRate);
        float phase = 0;
        for (int i = 0; i < len && start + i < n; ++i) {
            const float s = float(i) / kRate;
            const float f = tone.freq + tone.sweep * s;
            phase += kTau * f / kRate;
            const float attack = std::min(1.0f, s * 400.0f);  // ~2.5 ms fade-in avoids clicks
            const float release = std::min(1.0f, float(len - i) / (kRate * 0.004f));
            const float env = attack * release * std::exp(-tone.decay * s);
            mixbuf[size_t(start + i)] += tone.amp * env * (std::sin(phase) + 0.18f * std::sin(2 * phase));
        }
    }
    std::vector<uint8_t> wav(44 + size_t(n) * 2);
    auto put32 = [&](size_t at, uint32_t v) { std::memcpy(&wav[at], &v, 4); };
    auto put16 = [&](size_t at, uint16_t v) { std::memcpy(&wav[at], &v, 2); };
    std::memcpy(&wav[0], "RIFF", 4);
    put32(4, uint32_t(36 + n * 2));
    std::memcpy(&wav[8], "WAVEfmt ", 8);
    put32(16, 16);
    put16(20, 1);  // PCM
    put16(22, 1);  // mono
    put32(24, kRate);
    put32(28, kRate * 2);
    put16(32, 2);
    put16(34, 16);
    std::memcpy(&wav[36], "data", 4);
    put32(40, uint32_t(n * 2));
    for (int i = 0; i < n; ++i) {
        const float v = std::max(-1.0f, std::min(1.0f, mixbuf[size_t(i)] * volume));
        put16(44 + size_t(i) * 2, uint16_t(int16_t(v * 32000.0f)));
    }
    return wav;
}

}  // namespace

void SoundPlayer::configure(bool enabled, int volume) {
    enabled_ = enabled;
    volume_ = volume < 1 ? 1 : volume > 3 ? 3 : volume;
    if (!enabled_) PlaySoundW(nullptr, nullptr, 0);
}

void SoundPlayer::build() {
    const float vol = volume_ == 1 ? 0.18f : volume_ == 2 ? 0.35f : 0.6f;
    {
        const Tone t[] = {{0, 880, 0.09f, 0.7f, 38}, {0, 1320, 0.07f, 0.25f, 55}};
        wav_[int(Sfx::Place)] = render(t, 2, 0.1f, vol);
    }
    {
        const Tone t[] = {{0, 1568, 0.05f, 0.35f, 70}};
        wav_[int(Sfx::Note)] = render(t, 1, 0.06f, vol);
    }
    {
        const Tone t[] = {{0, 640, 0.08f, 0.5f, 40, -2400}};
        wav_[int(Sfx::Erase)] = render(t, 1, 0.09f, vol);
    }
    {
        const Tone t[] = {{0, 220, 0.1f, 0.8f, 18}, {0.11f, 185, 0.14f, 0.8f, 16}};
        wav_[int(Sfx::Error)] = render(t, 2, 0.26f, vol);
    }
    {
        const Tone t[] = {{0, 1047, 0.18f, 0.45f, 14}, {0.06f, 1319, 0.18f, 0.45f, 14}, {0.12f, 1568, 0.24f, 0.5f, 12}};
        wav_[int(Sfx::Unit)] = render(t, 3, 0.38f, vol);
    }
    {
        const Tone t[] = {{0, 523, 0.2f, 0.5f, 8},     {0.12f, 659, 0.2f, 0.5f, 8},  {0.24f, 784, 0.2f, 0.5f, 8},
                          {0.36f, 1047, 0.6f, 0.55f, 5}, {0.36f, 784, 0.6f, 0.3f, 5}, {0.36f, 659, 0.6f, 0.25f, 5}};
        wav_[int(Sfx::Win)] = render(t, 6, 1.0f, vol);
    }
    {
        const Tone t[] = {{0, 784, 0.16f, 0.45f, 12}, {0.08f, 1047, 0.16f, 0.45f, 12}, {0.16f, 1319, 0.3f, 0.5f, 9}};
        wav_[int(Sfx::Achievement)] = render(t, 3, 0.48f, vol);
    }
    builtVolume_ = volume_;
}

void SoundPlayer::play(Sfx s) {
    if (!enabled_) return;
    if (builtVolume_ != volume_) build();
    const auto& buf = wav_[int(s)];
    if (!buf.empty())
        PlaySoundW(reinterpret_cast<LPCWSTR>(buf.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

}  // namespace sudoku
