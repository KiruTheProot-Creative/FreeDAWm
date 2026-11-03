# Architecture

- `core/` (`fdm_core`, C++20, no dependencies): engine, clips, channels, built-in instruments, WAV I/O.
- `app/` (JUCE 9): GUI, audio/MIDI devices, VST3 hosting (`PluginHost`, `Vst3Effect : fdm::IEffect`), project files.
- `tests/`: 53 engine/instrument tests. `tools/render.cpp`: offline demo renderer.

## Engine
- 960 PPQ, 4/4. Sample-accurate: instruments render in segments between ticks; FX run on whole blocks.
- Lock-free: notes are packed into single atomic 64-bit words, params are atomics, live MIDI uses a spin-locked ring.
- `Engine::swapEffect` swaps FX while audio runs and waits for the audio block counter before returning the old effect.
- Structural changes (add/remove channel, load project) detach the audio callback first.
- `IEffect` is the plugin boundary; `Channel::external` can replace a built-in instrument (future VST instruments).

## Arrangement
Each channel holds up to 128 *placements* (start, length, clip slot), each packed into one atomic word like notes are. With `Engine::arrangementMode` on, the audio thread looks up the placement under the playhead every tick and plays that slot's clip, looping it inside the placement. The engine also owns the loop region and a seek request, so the UI can move the playhead while playing. `renderOffline(Engine::kRenderArrangement, ...)` bounces the whole timeline.

## Audio tracks
The engine owns a pool of up to 256 `AudioSample`s, which are decoded into memory on the UI thread and never change after that. Audio tracks hold up to 128 packed `AudioPlacement`s (start tick, length, sample index). Playback is stateless: each render segment works out its fractional tick position, then reads each overlapping clip with linear-interpolation resampling and 64-frame edge fades. That way seeking and looping need no extra bookkeeping. Samples are freed only while the audio callback is detached (new project, load).

## Roadmap
Hosted VST3 instruments, sampler (drag samples onto the timeline), plugin delay compensation, automation lanes, audio clips, MIDI export, effects (designed separately).
