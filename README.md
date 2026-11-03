# FreeDAWm

A free, channel-based DAW in C++ with an Ableton-style session workflow. Beat-making first.

## What works today
- **Layout**: REAPER-style transport and track lanes on top, an Ableton-style detail panel underneath, and an explorer sidebar on the left. The dark red look is inspired by Emulon X.
- **Arrangement timeline**: everything plays from the timeline.
  - MIDI tracks: double-click a lane to create a clip.
  - Audio tracks: drag a file from the Samples explorer (or your desktop) onto a lane. Dropping below the last track makes a new audio track. Double-click a lane to import a file.
  - Clips: drag to move, drag the right edge to resize. Ctrl+D duplicates, Del deletes, right-click shows more.
  - Ruler: click or drag to move the playhead. The thin strip above it sets the loop region.
  - Ctrl+wheel zooms.
- **Audio tracks**: play WAV, AIFF, FLAC, MP3 and OGG files at their own speed (no time-stretching yet). Any sample rate is resampled on the fly.
- **Bottom panel**:
  - *Piano Roll*: a step grid for drums, a piano roll for synths.
  - *FX Chain*: up to 64 VST3 effects per track, return and master. The chain grows as you add them.
  - *Mixer*: every track plus Return A/B and Master.
- **Explorer**:
  - *VST*: lists effects found in the `plugins/` folder (and any folders you add). Double-click one to add it to the selected track.
  - *Samples*: browses your sample folders. Click to preview, drag onto the timeline.
- **Instruments**: a temporary Drum Rack and Poly Synth so MIDI tracks make sound for now. They will be replaced by hosted instrument plugins.
- **Plugins**: VST3 hosting only. No effects or instruments are built in. VST2 is not supported.
- **Projects** (`.fdm`; sound files are referenced by path) and **WAV export** of the whole timeline (16/24/32-bit).
- The computer keyboard plays the selected MIDI track (A-K row, Z/X octave). Space plays and stops.

Known limits:
- A MIDI track holds 8 different clips. Copies of a clip share its notes; use "Make clip unique" to split them.
- Audio clips don't follow tempo changes.
- Clips can only be moved within their own track.

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
