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

## Roadmap
Arrangement view, plugin delay compensation, sampler, automation, MIDI export, effects (designed separately).
