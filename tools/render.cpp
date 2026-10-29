// Command-line offline renderer: builds the demo session and writes it to a WAV file.
// Handy for hearing the engine without a GUI or a sound card, and for regression checks.
//
//   fdm_render [out.wav] [scene=0] [bars=4] [bpm]

#include <cstdio>
#include <cstdlib>
#include <string>

#include "fdm/DemoProject.h"
#include "fdm/Engine.h"
#include "fdm/Wav.h"

int main(int argc, char** argv) {
    const std::string path = argc > 1 ? argv[1] : "demo.wav";
    const int scene = argc > 2 ? std::atoi(argv[2]) : 0;
    const int bars  = argc > 3 ? std::atoi(argv[3]) : 4;

    fdm::Engine engine;
    engine.prepare(48000.0, 512);
    fdm::buildDemoProject(engine);
    if (argc > 4) engine.bpm.set(static_cast<float>(std::atof(argv[4])));

    std::vector<float> left, right;
    engine.renderOffline(scene, bars, 1.0f, left, right);

    float peak = 0.0f;
    for (size_t i = 0; i < left.size(); ++i)
        peak = std::max({peak, std::fabs(left[i]), std::fabs(right[i])});

    if (!fdm::writeWav(path, left, right, 48000, fdm::WavFormat::Pcm24)) {
        std::fprintf(stderr, "could not write %s\n", path.c_str());
        return 1;
    }
    std::printf("wrote %s: scene %d, %d bars @ %.0f BPM, %.2f s, peak %.3f\n", path.c_str(), scene, bars,
                static_cast<double>(engine.bpm.get()), static_cast<double>(left.size()) / 48000.0,
                static_cast<double>(peak));
    return 0;
}
