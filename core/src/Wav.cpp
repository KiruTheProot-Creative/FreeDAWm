#include "fdm/Wav.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>

namespace fdm {

namespace {

void put16(std::ofstream& f, uint16_t v) { f.put(static_cast<char>(v & 0xff)); f.put(static_cast<char>(v >> 8)); }
void put32(std::ofstream& f, uint32_t v) { put16(f, static_cast<uint16_t>(v & 0xffff)); put16(f, static_cast<uint16_t>(v >> 16)); }

uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t get32(const uint8_t* p) { return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
                                        | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24); }

}  // namespace

bool writeWav(const std::string& path, const std::vector<float>& left, const std::vector<float>& right,
              int sampleRate, WavFormat format) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    const size_t frames = std::min(left.size(), right.size());
    const int bytes = format == WavFormat::Pcm16 ? 2 : format == WavFormat::Pcm24 ? 3 : 4;
    const uint32_t dataSize = static_cast<uint32_t>(frames * 2 * static_cast<size_t>(bytes));

    f.write("RIFF", 4);
    put32(f, 36 + dataSize);
    f.write("WAVEfmt ", 8);
    put32(f, 16);
    put16(f, format == WavFormat::Float32 ? 3 : 1);
    put16(f, 2);
    put32(f, static_cast<uint32_t>(sampleRate));
    put32(f, static_cast<uint32_t>(sampleRate * 2 * bytes));
    put16(f, static_cast<uint16_t>(2 * bytes));
    put16(f, static_cast<uint16_t>(bytes * 8));
    f.write("data", 4);
    put32(f, dataSize);

    uint32_t rng = 0x2545F491u;
    auto rand01 = [&rng]() {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return static_cast<float>(rng) * (1.0f / 4294967296.0f);
    };

    auto writeSample = [&](float x) {
        x = std::clamp(x, -1.0f, 1.0f);
        switch (format) {
            case WavFormat::Pcm16: {
                const float dither = (rand01() - rand01()) / 32768.0f;       // TPDF, +-1 LSB
                const int v = static_cast<int>(std::lround(std::clamp(x + dither, -1.0f, 1.0f) * 32767.0f));
                put16(f, static_cast<uint16_t>(static_cast<int16_t>(v)));
                break;
            }
            case WavFormat::Pcm24: {
                const int v = static_cast<int>(std::lround(x * 8388607.0f));
                f.put(static_cast<char>(v & 0xff));
                f.put(static_cast<char>((v >> 8) & 0xff));
                f.put(static_cast<char>((v >> 16) & 0xff));
                break;
            }
            case WavFormat::Float32: {
                uint32_t bits;
                std::memcpy(&bits, &x, 4);
                put32(f, bits);
                break;
            }
        }
    };

    for (size_t i = 0; i < frames; ++i) {
        writeSample(left[i]);
        writeSample(right[i]);
    }
    return static_cast<bool>(f);
}

bool readWav(const std::string& path, WavData& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (buf.size() < 44 || std::memcmp(buf.data(), "RIFF", 4) != 0 || std::memcmp(buf.data() + 8, "WAVE", 4) != 0)
        return false;

    int fmtTag = 0, channels = 0, bits = 0, sampleRate = 0;
    const uint8_t* data = nullptr;
    size_t dataSize = 0;

    size_t pos = 12;
    while (pos + 8 <= buf.size()) {
        const uint8_t* chunk = buf.data() + pos;
        const size_t size = get32(chunk + 4);
        if (std::memcmp(chunk, "fmt ", 4) == 0 && pos + 8 + 16 <= buf.size()) {
            fmtTag     = get16(chunk + 8);
            channels   = get16(chunk + 10);
            sampleRate = static_cast<int>(get32(chunk + 12));
            bits       = get16(chunk + 22);
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            data = chunk + 8;
            dataSize = std::min(size, buf.size() - pos - 8);
            break;
        }
        pos += 8 + size + (size & 1);
    }
    if (!data || channels < 1 || channels > 2) return false;
    if (!((fmtTag == 1 && (bits == 16 || bits == 24)) || (fmtTag == 3 && bits == 32))) return false;

    const size_t bytes = static_cast<size_t>(bits / 8);
    const size_t frames = dataSize / (bytes * static_cast<size_t>(channels));
    out.sampleRate = sampleRate;
    out.channels = channels;
    out.left.assign(frames, 0.0f);
    out.right.assign(frames, 0.0f);

    auto readSample = [&](const uint8_t* p) -> float {
        if (fmtTag == 3) { uint32_t b = get32(p); float v; std::memcpy(&v, &b, 4); return v; }
        if (bits == 16)  return static_cast<float>(static_cast<int16_t>(get16(p))) / 32768.0f;
        int v = p[0] | (p[1] << 8) | (p[2] << 16);
        if (v & 0x800000) v -= 0x1000000;
        return static_cast<float>(v) / 8388608.0f;
    };

    for (size_t i = 0; i < frames; ++i) {
        const uint8_t* p = data + i * bytes * static_cast<size_t>(channels);
        out.left[i] = readSample(p);
        out.right[i] = channels == 2 ? readSample(p + bytes) : out.left[i];
    }
    return true;
}

}  // namespace fdm
