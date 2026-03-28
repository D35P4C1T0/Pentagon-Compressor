Below is a clean implementation brief you can hand to a coding agent.

---

# VST Plugin Technical Spec

**Project name:** “Pentagon”
**Type:** Multi-stage compressor plugin
**Formats:** VST3 first, AU optional, CLAP optional
**Channels:** Mono + Stereo
**Primary concept:** 5-in-1 compressor where each compressor stage is chained into the next, with per-stage enable/bypass and optional drag-to-reorder signal flow.

## 1. Product summary

Build a dynamics plugin that contains **five serial compressor models** in one plugin:

1. **FET76**
2. **OPTO2A**
3. **VCA160**
4. **VARIMU**
5. **TUBE670**

The user can:

* enable/disable each stage
* adjust each stage’s core parameters
* chain all stages in series
* optionally **reorder the stages**
* use global **Dry/Wet**, **Output**, **Safety**, **Oversampling**, **Preset**, and **Tweak/Macro**
* view live **input/output meters** and **per-stage gain reduction meters**

The UI style is terminal/retro, but the DSP and parameter behavior should be modern and stable.

---

## 2. Signal flow

### Default serial order

From the prototype, the default chain appears to be:

**Input → FET76 → OPTO2A → VCA160 → VARIMU → TUBE670 → Output**

### Reordering requirement

Very important feature:

* The five compressor modules must be reorderable by the user.
* Reordering changes actual DSP signal flow, not just the UI.
* Minimum viable behavior:

  * drag-and-drop module blocks in the UI
  * or up/down / left/right move buttons
* When a stage is bypassed, it remains in the chain order but passes audio unchanged.

### Global dry/wet

Dry/Wet should be **post-chain parallel blend**:

* Dry path = input after input trim, before first compressor
* Wet path = output of the full serial chain
* Final output = dry/wet mix, then global output gain

Recommended order:
**Input trim → serial compressors → wet/dry mix → output gain → safety limiter/output protection**

Alternative acceptable order:
**Input trim → serial compressors → safety → wet/dry → output gain**

Pick one and document it consistently.

---

## 3. DSP modules

These do **not** need to be exact hardware emulations for v1, but each should have a distinct behavior profile.

## 3.1 FET76 stage

**Character:** fast, aggressive, peak-focused

### Parameters visible in prototype

* Enable
* Input
* Output
* Attack (µs)
* Release (ms)
* Threshold
* Sat
* GR FET meter

### Proposed implementation

* Compressor type: feed-forward or hybrid FET-style
* Detection: peak
* Stereo link: default on for stereo
* Soft knee optional internally
* Saturation block after gain cell or integrated drive stage

### Parameters

* `enabled` : bool
* `inputGainDb` : -24 dB to +24 dB
* `outputGainDb` : -24 dB to +24 dB
* `attackUs` : 20 µs to 800 µs
* `releaseMs` : 20 ms to 1200 ms
* `thresholdDb` : -60 dB to 0 dB
* `ratio` : fixed FET-style, or expose later

  * For v1, use fixed aggressive ratio, e.g. 4:1 or 8:1
* `saturationMode` : Off / Clean / Warm / Drive

  * Prototype shows `SAT CLEAN`
* Meter:

  * `gainReductionDb`

### Default values from mockup

* Enable: Off
* Input: 0.0
* Output: 0.0
* Attack: 120 µs
* Release: 220 ms
* Threshold: -24.0 dB
* Sat: Clean

---

## 3.2 OPTO2A stage

**Character:** smooth, optical, slower musical compression

### Parameters visible

* Enable
* Peak Red
* Mode
* Makeup
* HF Emp
* Sat
* GR OPTO meter

### Proposed implementation

* Compressor type: opto-style, program-dependent envelope
* Detection: RMS-ish / opto-slow peak hybrid
* Attack/release are largely program dependent and not directly exposed
* HF emphasis affects sidechain sensitivity to high frequencies

### Parameters

* `enabled` : bool
* `peakReduction` : 0 to 100
* `mode` : enum

  * `COMP`
  * `LIMIT`
* `makeupGainDb` : -12 dB to +24 dB
* `hfEmphasis` : 0 to 100%
* `saturationMode` : Off / Clean / Warm / Tube
* Meter:

  * `gainReductionDb`

### Default values from mockup

* Enable: On
* Peak Red: 71.5
* Mode: COMP
* Makeup: 0.0
* HF Emp: 0.0
* Sat: Clean

---

## 3.3 VCA160 stage

**Character:** punchy, controlled, classic VCA snap

### Parameters visible

* Enable
* Threshold
* Overeasy
* Makeup
* Sat
* GR VCA meter

### Proposed implementation

* Compressor type: VCA
* Detection: peak or fast RMS hybrid
* Overeasy = soft-knee switch
* Ratio can be fixed for v1, or add later

### Parameters

* `enabled` : bool
* `thresholdDb` : -60 dB to 0 dB
* `overeasy` : bool
* `makeupGainDb` : -24 dB to +24 dB
* `saturationMode` : Off / Clean / Punch / Warm
* Optional internal fixed ratio:

  * 4:1 or user-selectable later
* Meter:

  * `gainReductionDb`

### Default values from mockup

* Enable: On
* Threshold: -28.6 dB
* Overeasy: On
* Makeup: 0.0
* Sat: Clean

---

## 3.4 VARIMU stage

**Character:** glue, smooth bus compression, slower timing

### Parameters visible

* Enable
* Threshold
* Attack
* Recovery
* Makeup
* Sat
* GR VMU meter

### Proposed implementation

* Compressor type: vari-mu style
* Detection: RMS / program-dependent
* Recovery can be stepped or continuous
* Attack and release should feel slower and smoother than FET/VCA

### Parameters

* `enabled` : bool
* `thresholdDb` : -60 dB to 0 dB
* `attackMs` : 0.1 ms to 100 ms
* `recovery` : enum or continuous

  * Example enum:

    * Fast
    * Medium
    * Slow
    * Auto
  * Prototype shows `<0.4s>`
* `makeupGainDb` : -24 dB to +24 dB
* `saturationMode` : Off / Clean / Warm / Thick
* Meter:

  * `gainReductionDb`

### Default values from mockup

* Enable: Off
* Threshold: -22.0 dB
* Attack: 45 ms
* Recovery: 0.4 s
* Makeup: 0.0
* Sat: Clean

---

## 3.5 TUBE670 stage

**Character:** tube variable-mu / limiter style, thick and smooth

### Parameters visible

* Enable
* Threshold
* Time Const
* Mode
* Makeup
* Sat
* GR 670 meter

### Proposed implementation

* Compressor type: tube limiter / variable-mu style
* Time constant should emulate stepped attack/release combinations
* Mode is visible as `<LR>` in mockup; likely stereo linking mode

### Parameters

* `enabled` : bool
* `thresholdDb` : -60 dB to 0 dB
* `timeConstant` : stepped enum

  * values 1 to 6
  * prototype shows `<2>`
* `mode` : enum

  * `LR` = linked stereo
  * `MID/SIDE` optional later
  * `DUAL` optional later
* `makeupGainDb` : -24 dB to +24 dB
* `saturationMode` : Off / Clean / Tube / Heavy
* Meter:

  * `gainReductionDb`

### Default values from mockup

* Enable: Off
* Threshold: -18.0 dB
* Time Const: 2
* Mode: LR
* Makeup: 0.0
* Sat: Clean

---

## 4. Global section

From the UI, the global panel contains:

* Dry/Wet
* Output
* Safety
* Oversampling
* Preset
* Tweak
* Input meter readout

## 4.1 Global parameters

* `dryWet` : 0.0 to 1.0

  * default: 0.71
* `outputGainDb` : -24 dB to +24 dB

  * default: 0.0
* `safetyEnabled` : bool

  * default: On
* `oversampling` : enum

  * 1x
  * 2x
  * 4x
  * optional 8x if CPU allows
  * default shown: 1x
* `presetName` : string

  * example shown: `VOICE RADIO`
* `tweakStyle` : enum or macro

  * example shown: `AGGRESSIVE`

## 4.2 Tweak control

The “Tweak” field looks like a macro/character selector.
Implement as a top-level macro system that modifies internal values across all enabled stages.

Suggested modes:

* Clean
* Balanced
* Aggressive
* Glue
* Loud
* Vocal
* Drum Smash

Prototype default:

* `AGGRESSIVE`

For v1, this can simply offset:

* thresholds
* attack/release profiles
* saturation amount
* wet gain compensation

---

## 5. Metering

The prototype includes a dedicated meter panel:

* Input level meter + dBFS readout
* Output level meter + dBFS readout
* GR FET
* GR OPTO
* GR VCA
* GR VMU
* GR 670

## 5.1 Meter requirements

Expose and render:

* `inputPeakDbFs`
* `outputPeakDbFs`
* `gainReductionFetDb`
* `gainReductionOptoDb`
* `gainReductionVcaDb`
* `gainReductionVariMuDb`
* `gainReduction670Db`

Recommended:

* peak-hold for input/output
* smooth UI meter ballistics
* audio-thread-safe meter publishing using atomics or lock-free transfer

---

## 6. Reordering system

This is a major feature and should be treated as core architecture, not a UI hack.

## 6.1 Chain representation

Maintain chain as an ordered array of stage IDs, e.g.

```cpp
enum class StageType { FET76, OPTO2A, VCA160, VARIMU, TUBE670 };
std::array<StageType, 5> chainOrder;
```

Default:

```cpp
[FET76, OPTO2A, VCA160, VARIMU, TUBE670]
```

User reordering updates `chainOrder`.

## 6.2 Processing logic

For each block:

1. copy input to dry buffer
2. process signal through enabled stages in `chainOrder`
3. compute wet signal
4. dry/wet mix
5. output gain
6. safety limiter if enabled
7. update meters

## 6.3 UI behavior

Each module panel should support:

* drag handle
* visual order indicator
* drop target highlight
* persistence in plugin state / preset

---

## 7. Safety / protection

“Safety” likely means protection against clipping or unstable gain staging.

Implement:

* `safetyEnabled`
* output protection stage:

  * transparent soft clip or true-peak-ish limiter
  * ceiling default: -0.5 dBFS or -1.0 dBFS
* anti-denormal handling
* parameter smoothing
* oversampling-aware limiter if clipper is nonlinear

Recommended v1 behavior:

* lightweight brickwall limiter or soft clipper on final output
* only active when Safety = On

---

## 8. Oversampling

Prototype shows oversampling control.

## Requirements

* oversampling applies to nonlinear stages and possibly full chain
* at minimum:

  * 1x
  * 2x
  * 4x
* latency must be reported correctly to host
* switching oversampling should be click-safe:

  * either deferred until transport stop
  * or crossfaded / rebuilt safely

Recommended architecture:

* full wet path oversampled
* dry path kept native and latency compensated
* nonlinear saturation blocks benefit most from OS

---

## 9. Parameter smoothing and automation

All continuous parameters should be automatable and smoothed.

### Smoothing required for

* thresholds
* gains
* dry/wet
* attack/release if continuously variable
* macro/tweak amounts

### Suggestions

* use sample-accurate smoothing where practical
* no zipper noise
* bypass changes should crossfade over a short window, e.g. 5–20 ms

---

## 10. State and preset management

Plugin state must serialize:

* all global parameters
* all stage parameters
* stage enable states
* stage order
* selected preset
* tweak mode
* oversampling mode
* UI preferences if any

Preset system:

* factory presets from mockup style
* user presets storable in host state first, filesystem browser optional later

---

## 11. UI spec extracted from prototype

## Layout

Two-column grid of modules plus top control bar:

* Top left: global controls
* Top right: meters
* Middle/lower: compressor modules
* One extra “Coming Soon” slot shown; do not implement as functional in v1 unless planned

### Panels visible

* Global
* Meters
* FET76
* OPTO2A
* VCA160
* VARIMU
* TUBE670
* Coming Soon placeholder

## Visual style

* black background
* green monospace text
* yellow/gold panel borders
* terminal / CRT aesthetic
* bracketed section headers like `[ FET76 ]`

## Widgets

Use simple components:

* toggle buttons: `[ON] / [OFF]`
* stepped selectors: `<COMP>`, `<2>`, `<LR>`
* horizontal bars for values
* meter bars for GR and IO

---

## 12. Internal parameter model

Use stable parameter IDs, for example:

### Global

* `global_drywet`
* `global_output`
* `global_safety`
* `global_oversampling`
* `global_preset`
* `global_tweak`

### FET76

* `fet_enable`
* `fet_input`
* `fet_output`
* `fet_attack_us`
* `fet_release_ms`
* `fet_threshold`
* `fet_sat`

### OPTO2A

* `opto_enable`
* `opto_peak_reduction`
* `opto_mode`
* `opto_makeup`
* `opto_hf_emp`
* `opto_sat`

### VCA160

* `vca_enable`
* `vca_threshold`
* `vca_overeasy`
* `vca_makeup`
* `vca_sat`

### VARIMU

* `varimu_enable`
* `varimu_threshold`
* `varimu_attack`
* `varimu_recovery`
* `varimu_makeup`
* `varimu_sat`

### TUBE670

* `tube670_enable`
* `tube670_threshold`
* `tube670_timeconst`
* `tube670_mode`
* `tube670_makeup`
* `tube670_sat`

### Chain order

Store as ordered stage IDs:

* `chain_slot_1`
* `chain_slot_2`
* `chain_slot_3`
* `chain_slot_4`
* `chain_slot_5`

---

## 13. Recommended architecture

## Core classes

* `PluginProcessor`
* `PluginEditor`
* `CompressorStage` interface
* `Fet76Stage`
* `Opto2AStage`
* `Vca160Stage`
* `VariMuStage`
* `Tube670Stage`
* `StageChainManager`
* `OversamplingEngine`
* `MeterBus`
* `SafetyLimiter`

## CompressorStage interface

```cpp
class CompressorStage {
public:
    virtual void prepare(double sampleRate, int maxBlockSize, int numChannels) = 0;
    virtual void reset() = 0;
    virtual void setParameters(const StageParams& params) = 0;
    virtual void process(AudioBuffer<float>& buffer) = 0;
    virtual float getGainReductionDb() const = 0;
    virtual bool isEnabled() const = 0;
    virtual ~CompressorStage() = default;
};
```

## Chain manager

Responsible for:

* maintaining order
* calling enabled stages in order
* exposing per-stage GR
* hot-swapping UI order safely

---

## 14. Performance targets

* real-time safe audio thread
* no allocations in process block
* no locks on audio thread
* oversampling modes should not spike CPU excessively
* target latency:

  * 1x minimal
  * 2x / 4x reported properly

---

## 15. MVP vs later versions

## MVP

* 5 compressor stages
* serial chaining
* stage enable/bypass
* reorderable chain
* dry/wet
* output
* safety
* oversampling 1x/2x/4x
* meters
* preset save/restore
* stereo support

## Later

* exact analog modeling
* M/S mode in some stages
* external sidechain
* advanced ratio controls
* lookahead
* per-stage mix knobs
* undo/redo for chain reordering
* “Coming Soon” sixth slot

---

## 16. Ambiguities from the prototype

These are visible but not fully defined by the mockup, so the coding agent should treat them as inferred requirements:

* exact ratio behavior for each compressor
* exact saturation modes and curves
* exact meaning of “Tweak”
* exact meaning of Tube670 “Mode”
* whether Safety is clipper vs limiter
* exact oversampling choices beyond 1x

Those should be implemented as **reasonable v1 interpretations** with clean abstractions so they can be refined later.

---

# Final handoff summary for coding agent

Implement a **5-stage serial compressor plugin** with these modules: **FET76, OPTO2A, VCA160, VARIMU, TUBE670**.
Each stage has:

* its own enable switch
* its own characteristic parameter set
* its own gain reduction meter

The plugin also has:

* global dry/wet
* global output gain
* safety output protection
* oversampling
* preset selector
* tweak macro
* input/output meters

Most important architectural feature:

* **the 5 modules must be reorderable by the user**
* reordering must change real DSP chain order
* order must serialize in preset/state

Use a modular DSP architecture so each compressor is its own processing object, then run them in the current chain order.

