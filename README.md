# Pentagon

`Pentagon` is a JUCE-based VST3 dynamics plugin implementing the five-stage serial compressor described in [`reference/specs.md`](/home/matteo/Documents/prog/vst/5dita_2/reference/specs.md) and [`reference/specs_long.md`](/home/matteo/Documents/prog/vst/5dita_2/reference/specs_long.md).

Implemented features:

- Five serial compressor stages: `FET76`, `OPTO2A`, `VCA160`, `VARIMU`, `TUBE670`
- Per-stage enable, parameter controls, saturation mode, and GR meter
- Reorderable chain with persistent stage order
- Global `Dry/Wet`, `Output`, `Safety`, `Oversampling`, `Tweak`, and preset selector
- Input/output metering
- Oversampled wet path with dry-path latency compensation
- Linux VST3 build target

## Requirements

- CMake `>= 3.22`
- C++20 compiler
- JUCE `8.0.7`
- Linux development packages for ALSA, OpenGL, Freetype, Fontconfig, and GTK/WebKit if you build with JUCE FetchContent

## Build

The examples below use half the available CPU cores for the build job count.

If `JUCE_DIR` is available locally:

```bash
cmake -S . -B build -DJUCE_DIR=/path/to/JUCE
jobs=$(( $(nproc) / 2 ))
[ "$jobs" -lt 1 ] && jobs=1
cmake --build build --config Release -j"$jobs"
```

Otherwise the project can fetch JUCE automatically during configure:

```bash
cmake -S . -B build
jobs=$(( $(nproc) / 2 ))
[ "$jobs" -lt 1 ] && jobs=1
cmake --build build --config Release -j"$jobs"
```

Built artifact on Linux:

```text
build/Pentagon_artefacts/VST3/Pentagon.vst3
```

Main binary inside the bundle:

```text
build/Pentagon_artefacts/VST3/Pentagon.vst3/Contents/x86_64-linux/Pentagon.so
```

## Highlights

- `FET76` exposes the classic `1176` ratios: `4:1`, `8:1`, `12:1`, `20:1`
- All saturating stages expose both a saturation mode selector and a `SAT MIX` control
- `TUBE670` supports `LR` and `MS` stereo modes
- Presets persist stage order alongside per-stage parameter values

## Project Layout

- [`CMakeLists.txt`](/home/matteo/Documents/prog/vst/5dita_2/CMakeLists.txt): JUCE plugin target and build configuration
- [`Source/PluginProcessor.cpp`](/home/matteo/Documents/prog/vst/5dita_2/Source/PluginProcessor.cpp): DSP graph, oversampling, dry/wet, safety, state, presets
- [`Source/StageProcessors.h`](/home/matteo/Documents/prog/vst/5dita_2/Source/StageProcessors.h): stage-specific compressor implementations
- [`Source/PluginEditor.cpp`](/home/matteo/Documents/prog/vst/5dita_2/Source/PluginEditor.cpp): retro UI, meters, preset/global controls, stage cards
- [`docs/implementation.md`](/home/matteo/Documents/prog/vst/5dita_2/docs/implementation.md): implementation notes and tradeoffs
