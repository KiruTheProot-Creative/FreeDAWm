#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace fdm {

/** A sound file loaded into memory, ready for audio tracks to play. Immutable once handed to the engine. */
struct AudioSample {
    static constexpr int kPeakBlock = 256;     // frames per waveform-overview point

    std::string path;          // where it came from (saved in projects)
    std::string name;
    double sampleRate = 48000.0;
    std::vector<float> left, right;            // right == left for mono files
    std::vector<float> peaks;                  // max |x| per kPeakBlock frames, for drawing

    int64_t frames() const { return static_cast<int64_t>(left.size()); }
    double  seconds() const { return sampleRate > 0.0 ? static_cast<double>(frames()) / sampleRate : 0.0; }

    void computePeaks() {
        peaks.assign(static_cast<size_t>((frames() + kPeakBlock - 1) / kPeakBlock), 0.0f);
        for (size_t i = 0; i < left.size(); ++i) {
            const float v = std::max(std::fabs(left[i]), std::fabs(right[i]));
            float& p = peaks[i / kPeakBlock];
            p = std::max(p, v);
        }
    }
};

}  // namespace fdm
