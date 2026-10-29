#pragma once

#include <string>
#include <vector>

namespace fdm {

enum class WavFormat { Pcm16, Pcm24, Float32 };

/** Writes a stereo WAV file. 16-bit output is dithered (TPDF). Returns false on I/O failure. */
bool writeWav(const std::string& path, const std::vector<float>& left, const std::vector<float>& right,
              int sampleRate, WavFormat format = WavFormat::Pcm24);

struct WavData {
    int sampleRate = 0;
    int channels   = 0;
    std::vector<float> left, right;   // right == left for mono files
};

/** Reads 16/24-bit PCM or 32-bit float WAV files. Returns false if the file is not a supported WAV. */
bool readWav(const std::string& path, WavData& out);

}  // namespace fdm
