# Implementation Notes

## Architecture

The plugin is split into three main layers:

- `PluginProcessor`: owns parameters, preset application, chain order state, oversampling, dry/wet, safety, and meter publication
- `StageChainManager`: decodes the current packed chain order and applies the five stage processors in that order
- `PluginEditor`: renders a retro terminal-style UI with global controls and stage cards

## Reorderable Chain

The stage order is stored as a packed `uint32_t` permutation and mirrored into plugin state. The audio thread reads the packed order atomically at block boundaries, so UI reorder operations do not require locks.

## DSP Model

Each stage is a distinct v1 behavior model rather than a strict hardware emulation:

- `FET76`: fast peak compressor with input/output trim and aggressive ratio
- `OPTO2A`: slower optical leveling with program-dependent release and HF-weighted sidechain
- `VCA160`: punchy RMS/peak hybrid with optional soft-knee `Overeasy`
- `VARIMU`: slower RMS glue compressor with stepped recovery behavior
- `TUBE670`: stepped timing compressor with thicker saturation voicing

Continuous controls use smoothing, and stage bypass transitions are mixed through a short smoothed enable amount to avoid hard discontinuities.

## Oversampling and Dry Path

The wet path supports `1x`, `2x`, and `4x` modes through JUCE oversampling blocks. The dry path remains at native rate and is latency-compensated with a ring-buffer delay before final dry/wet mixing.

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

They update both stage parameters and chain order.

## Current Scope

This is a functional v1 implementation aimed at the behavior described in the spec. It does not attempt exact analogue circuit modeling or a drag-and-drop reorder UI yet; reordering is exposed through move-left/move-right controls per stage.
