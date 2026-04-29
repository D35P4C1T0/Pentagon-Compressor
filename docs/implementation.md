# Implementation Notes

## Architecture

The plugin is split into three main layers:

- `PluginProcessor`: owns parameters, preset and user-slot snapshots, chain order state, deferred oversampling, output finishing, and meter publication
- `StageChainManager`: decodes the current packed chain order and applies the five stage processors in that order
- `PluginEditor`: renders a retro terminal-style UI with global controls and stage cards

## Reorderable Chain

The stage order is stored as a packed `uint32_t` permutation and mirrored into plugin state. The audio thread reads the packed order atomically at block boundaries, so UI reorder operations do not require locks.

The chain can run in three routing modes:

- `Serial`: each stage feeds the next stage in the current order
- `Parallel`: every stage receives the same input and the five outputs are summed at unity-safe weighting
- `Hybrid`: the first two ordered stages run as parallel lanes, then the result feeds the remaining stages serially

## DSP Model

Each stage is a distinct v1 behavior model rather than a strict hardware emulation:

- `FET76`: fast peak compressor with input/output trim and aggressive ratio
- `OPTO2A`: slower optical leveling with program-dependent release and HF-weighted sidechain
- `VCA160`: punchy RMS/peak hybrid with optional soft-knee `Overeasy`
- `VARIMU`: slower RMS glue compressor with stepped recovery behavior
- `TUBE670`: stepped timing compressor with thicker saturation voicing

Continuous controls use smoothing, stage bypass transitions are mixed through a short smoothed enable amount to avoid hard discontinuities, and each stage now has a more distinct detector/saturation voicing. Each stage also has its own wet mix, sidechain high-pass filter, and saturation placement selector (`Post`, `Pre`, `Both`).

## Oversampling and Dry Path

The wet path supports `1x`, `2x`, and `4x` modes through JUCE oversampling blocks. Oversampling mode changes are deferred until a safe low-level block instead of hard-switching mid-program. The dry path remains at native rate and is latency-compensated with a ring-buffer delay before final dry/wet mixing.

## Output Finish

The v2 output stage adds:

- slower loudness-matching auto gain
- a ceiling control
- a lookahead limiter path behind the `Safety` toggle
- limiter and auto-gain meters in the editor

## Build Notes

The recommended local build flow is:

```bash
cmake -S . -B build
jobs=$(( $(nproc) / 2 ))
[ "$jobs" -lt 1 ] && jobs=1
cmake --build build --config Release -j"$jobs"
```

## Presets

Factory presets are implemented as internal parameter/state snapshots in `Pentagon`:

- `VOICE RADIO`
- `DRUM SMASH`
- `GLUE BUS`
- `VOCAL LEVEL`

They update both stage parameters and chain order. The editor also exposes four persistent user preset slots plus A/B comparison captures.

## Current Scope

This is still a behavior-first implementation rather than a circuit-accurate hardware emulation, but it now includes drag reordering, collapsible stage cards, per-stage `SOLO` / `DELTA` audition, persistent user slots, and a JUCE smoke-test target.
