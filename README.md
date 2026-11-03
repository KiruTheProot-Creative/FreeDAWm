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
- **Instruments**: any VST3 or VST2 instrument plugin (Kontakt, Serum, Vital...) on a MIDI track.
  - Double-click it in the explorer's *Instruments* section to load it onto the selected MIDI track. If an audio track is selected, it goes onto a new MIDI track.
  - Or use "Load plugin..." on the instrument card in the FX Chain tab.
  - Notes reach the plugin sample-accurately, while the plugin still renders normal-sized blocks.
  - The plugin's state is saved with the project.
- **Tempo sync**: every plugin, effect or instrument, VST3 or VST2, gets the song's tempo, position, bar, time signature, play state and loop region. Synced delays, LFOs and arpeggiators follow the song.
  - A temporary Drum Rack and Poly Synth remain as fallbacks.
- **Plugins**: VST3, and VST2 through FreeDAWm's own clean-room host (see below). No effects or instruments are built in.
- **Projects** (`.fdm`; sound files are referenced by path) and **WAV export** of the whole timeline (16/24/32-bit).
- **Preferences** (toolbar button or Ctrl+,): ten tabs.
  - General, Audio, MIDI, Plugins, Library, Recording, Export, Editing, Appearance, Shortcuts.
  - Every setting that has a feature behind it works now: startup project, autosave and backups, audio device and test tone, MIDI inputs, plugin folders and scanning, sample folders and preview, export format, sample rate, tail, normalize and dither, timeline snap and wheel zoom, clip defaults, accent colour, UI scale, track height, meters, tooltips, and remappable shortcuts.
  - Settings for features that don't exist yet (recording, undo, MIDI clock, time-stretching and so on) are shown but marked *Coming soon*.
- The computer keyboard plays the selected MIDI track (A-K row, Z/X octave). Space plays and stops.

Known limits:
- A MIDI track holds 8 different clips. Copies of a clip share its notes; use "Make clip unique" to split them.
- Audio clips don't follow tempo changes.
- Clips can only be moved within their own track.

## VST2 support
FreeDAWm hosts VST 2.x plugins without Steinberg's VST2 SDK. `app/Source/vst2/Vst2Abi.h` is FreeDAWm's own description of the binary interface: the struct layout, function signatures and opcode numbers. It was written from scratch for interoperability, with its own names. `Vst2Format` plugs it into JUCE's plugin scanner.
- Supported: effects and instruments, parameters, programs, state chunks, MIDI in, transport info, editors (Windows, macOS, Linux X11), and shell plugins (several plugins in one file).
- Not supported: 32-bit plugins (a 64-bit host can't load them), double-precision processing, and plugins sending MIDI out.
- `app/tests/` holds a small VST2 plugin and a host test that loads it.
- VST2 can be switched off in Preferences > Plugins.

## Plugin scanning
Each plugin file (VST3 and VST2) is scanned in a separate process: FreeDAWm re-launches itself with `--fdm-scan-plugin`. A plugin that crashes or hangs (3-minute limit) only takes that helper process down. It's then listed under Preferences > Plugins > Blocked plugins, and you can unblock it to retry. VST2 libraries stay loaded once opened, because some plugins keep threads running after they're closed.

VST is a trademark of Steinberg Media Technologies GmbH. FreeDAWm isn't affiliated with or endorsed by Steinberg.

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
