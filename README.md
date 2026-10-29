# FreeDAWm

A free, channel-based DAW in C++ with an Ableton-style session workflow. Beat-making first.

## What works today
- **Session view**: up to 12 channels x 8 clip slots, scene launch, quantized launching, 2 return channels, master.
- **Instruments**: built-in Drum Rack (8 synthesized pads) and Poly Synth.
- **Clip editor**: step grid for drums, piano roll for synths.
- **Mixer**: volume, balance, mute/solo, 2 sends, meters, 4 FX slots per channel/return/master.
- **VST3 hosting**: effects load from the `plugins/` folder shipped with the app. The effects themselves are not built yet.
- **Projects** (`.fdm`) and **WAV export** (16/24/32-bit, deterministic offline render).
- Computer keyboard plays the selected channel (A-K row, Z/X octave); space = play/stop.

## Build
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build
```
Linux needs: `libasound2-dev libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libfreetype-dev libfontconfig1-dev libgl1-mesa-dev`.
Use `-DFDM_BUILD_APP=OFF` to build only the JUCE-free core, tests and the `fdm_render` tool.

## Licensing note
The app uses JUCE 9, which is AGPLv3 or commercial. Distributing FreeDAWm means choosing a licence compatible with that (or buying a JUCE licence).
The audio engine in `core/` does not depend on JUCE. No LICENSE file is included yet; that choice is yours.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
