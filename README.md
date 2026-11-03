# FreeDAWm

A free, channel-based DAW in C++ with an Ableton-style session workflow. Beat-making first.

## What works today
- **Layout**: REAPER-style top transport and track lanes, an Ableton-style detail panel underneath, and an explorer sidebar on the left. Dark red look inspired by Emulon X.
- **Arrangement timeline**: clips are placed on a bar ruler per track. Double-click a lane to create a clip, drag to move, drag the right edge to resize, Ctrl+D duplicates, Del deletes, right-click for more. Click or drag the ruler to move the playhead; drag the thin strip above it to set the loop region. Ctrl+wheel zooms.
- **Bottom panel**: *Piano Roll* (step grid for drums, piano roll for synths), *FX Chain* (up to 64 VST3 effects per track, return and master; the chain grows as you add them) and *Session* (Ableton-style clip launcher and mixer). The ARRANGE / SESSION buttons choose what the transport plays.
- **Explorer**: *VST* lists effects found in the `plugins/` folder (and any folders you add); double-click one to put it on the selected track. *Samples* browses your sample folders and previews files on click.
- **Instruments**: a placeholder Drum Rack and Poly Synth so you can make sound for now. They will be replaced by hosted instrument plugins.
- **Plugins**: VST3 hosting only. No effects or instruments are built in. VST2 is not supported.
- **Projects** (`.fdm`) and **WAV export** (arrangement, scene or what's playing; 16/24/32-bit).
- Computer keyboard plays the selected track (A-K row, Z/X octave); space = play/stop.

Known limits: a track holds 8 different clips (placements of the same clip share its notes; use "Make clip unique" to split them), samples can be previewed but not yet placed on the timeline (no sampler yet), and the arrangement clips can only be moved within their own track.

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
