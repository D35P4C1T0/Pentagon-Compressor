Here’s a coding-agent-ready handoff in two forms: a structured engineering spec and a JSON config schema.

---

# Engineering Spec for Coding Agent

## Project

**Name:** Pentagon
**Type:** Audio plugin
**Formats:** VST3 required, AU optional, CLAP optional
**Targets:** macOS + Windows
**Channels:** Mono, Stereo
**Core concept:** Five compressor models in one plugin, arranged in a serial chain with user-reorderable stage order.

---

## Product Requirements

### PR-001: Core signal chain

Implement a plugin containing 5 compressor stages:

1. FET76
2. OPTO2A
3. VCA160
4. VARIMU
5. TUBE670

Default chain order:

```text
Input -> FET76 -> OPTO2A -> VCA160 -> VARIMU -> TUBE670 -> Wet/Dry -> Output -> Safety
```

Only enabled stages process audio. Disabled stages pass signal through unchanged.

### PR-002: Reorderable chain

The user must be able to reorder the five stages.

Requirements:

* Reordering changes actual DSP processing order.
* Reordering persists in plugin state and presets.
* UI must expose drag-and-drop or move-left/move-right controls.
* Reordering must be safe during playback.
* If order changes during audio playback, either:

  * apply at block boundary, or
  * crossfade old/new chain for click-free behavior.

### PR-003: Global controls

Implement these global controls:

* Dry/Wet: 0.0 to 1.0
* Output Gain: -24 dB to +24 dB
* Safety: on/off
* Oversampling: 1x / 2x / 4x
* Preset selector
* Tweak macro / voicing selector

Default values from prototype:

* Dry/Wet = 0.71
* Output = 0.0 dB
* Safety = On
* Oversampling = 1x
* Preset = VOICE RADIO
* Tweak = AGGRESSIVE

### PR-004: Metering

Implement:

* Input peak meter in dBFS
* Output peak meter in dBFS
* Per-stage gain reduction meters:

  * GR FET
  * GR OPTO
  * GR VCA
  * GR VMU
  * GR 670

UI meters must be smoothed and audio-thread safe.

### PR-005: Visual style

UI should follow the prototype:

* black background
* green monospace text
* gold/yellow borders
* bracketed headers like `[ FET76 ]`
* simple retro/terminal look

This is styling only; it must not constrain DSP architecture.

---

## DSP Requirements

## DR-001: Audio threading

* No dynamic allocation in processBlock/audio callback
* No locks on audio thread
* No blocking calls on audio thread
* Use atomics or lock-free meter exchange for UI

## DR-002: Parameter smoothing

Smooth all continuous parameters:

* threshold
* gain
* dry/wet
* attack/release where applicable
* macro/tweak effects

Target:

* no zipper noise
* bypass toggles crossfaded over ~5–20 ms

## DR-003: Wet/dry topology

Recommended topology:

```text
input
 -> save dry copy
 -> process wet chain through enabled stages in current order
 -> mix dry/wet
 -> apply output gain
 -> apply safety stage
 -> output
```

Dry path must be latency compensated if oversampling or lookahead creates latency on the wet path.

## DR-004: Safety stage

Implement Safety as a transparent output protection stage.

v1 acceptable implementations:

* soft clipper with ceiling
* simple brickwall limiter
* limiter with ceiling at -1.0 dBFS or -0.5 dBFS

Requirements:

* togglable on/off
* stable under hot signals
* oversampling-aware if nonlinear

## DR-005: Oversampling

Implement:

* 1x
* 2x
* 4x

Requirements:

* host-reported latency must be correct
* switching OS mode must be click-safe
* nonlinear stages should benefit from oversampling

Recommended:

* oversample full wet path
* keep dry path native, compensate latency before mix

---

## Stage Specifications

## ST-001: FET76

**Goal:** Fast, aggressive, peak-forward compressor.

Parameters:

* enabled: bool
* inputGainDb: -24 to +24
* outputGainDb: -24 to +24
* attackUs: 20 to 800
* releaseMs: 20 to 1200
* thresholdDb: -60 to 0
* saturationMode: enum
* gainReductionDb: meter only

Defaults:

* enabled = false
* inputGainDb = 0.0
* outputGainDb = 0.0
* attackUs = 120
* releaseMs = 220
* thresholdDb = -24.0
* saturationMode = Clean

DSP notes:

* peak detector
* aggressive ratio internally
* fast envelope
* optional FET-like nonlinear transfer

## ST-002: OPTO2A

**Goal:** Smooth optical leveling.

Parameters:

* enabled: bool
* peakReduction: 0 to 100
* mode: COMP | LIMIT
* makeupGainDb: -12 to +24
* hfEmphasis: 0 to 100
* saturationMode: enum
* gainReductionDb: meter only

Defaults:

* enabled = true
* peakReduction = 71.5
* mode = COMP
* makeupGainDb = 0.0
* hfEmphasis = 0.0
* saturationMode = Clean

DSP notes:

* program-dependent envelope
* slower optical behavior
* HF emphasis modifies sidechain weighting

## ST-003: VCA160

**Goal:** Punchy VCA compression.

Parameters:

* enabled: bool
* thresholdDb: -60 to 0
* overeasy: bool
* makeupGainDb: -24 to +24
* saturationMode: enum
* gainReductionDb: meter only

Defaults:

* enabled = true
* thresholdDb = -28.6
* overeasy = true
* makeupGainDb = 0.0
* saturationMode = Clean

DSP notes:

* fast punchy character
* Overeasy = soft knee mode
* ratio may be fixed in v1

## ST-004: VARIMU

**Goal:** Smooth glue/bus style compression.

Parameters:

* enabled: bool
* thresholdDb: -60 to 0
* attackMs: 0.1 to 100
* recovery: enum or continuous
* makeupGainDb: -24 to +24
* saturationMode: enum
* gainReductionDb: meter only

Defaults:

* enabled = false
* thresholdDb = -22.0
* attackMs = 45
* recovery = 0.4s
* makeupGainDb = 0.0
* saturationMode = Clean

DSP notes:

* slower, smoother, more glue-like
* release should be program-dependent or recovery-profile-based

## ST-005: TUBE670

**Goal:** Thick tube limiter/vari-mu style compression.

Parameters:

* enabled: bool
* thresholdDb: -60 to 0
* timeConstant: enum 1..6
* mode: LR initially
* makeupGainDb: -24 to +24
* saturationMode: enum
* gainReductionDb: meter only

Defaults:

* enabled = false
* thresholdDb = -18.0
* timeConstant = 2
* mode = LR
* makeupGainDb = 0.0
* saturationMode = Clean

DSP notes:

* stepped timing behavior
* linked stereo in v1
* mode abstraction should allow future M/S or dual-mono

---

## Macro / Tweak System

## TW-001: Tweak selector

Implement a top-level tweak/voicing macro.

Minimum enum set:

* Clean
* Balanced
* Aggressive
* Glue
* Vocal
* Loud

Default:

* Aggressive

Behavior:

* modifies internal stage response, not UI layout
* may adjust:

  * attack/release curves
  * saturation intensity
  * threshold offsets
  * auto makeup tendency

Must be serializable and automatable.

---

## State Management

## SM-001: Persisted state

State must serialize:

* all global parameters
* all per-stage parameters
* all enable states
* full stage order
* oversampling mode
* tweak mode
* selected preset or preset reference
* UI ordering state

## SM-002: Presets

Support:

* host state save/restore
* factory preset definitions
* v1 can keep presets internal, filesystem preset browser optional

---

## Suggested Class Architecture

```text
PluginProcessor
PluginEditor
StageChainManager
MeterBridge
SafetyLimiter
OversamplingEngine

ICompressorStage
  Fet76Stage
  Opto2AStage
  Vca160Stage
  VariMuStage
  Tube670Stage
```

### Interface suggestion

```cpp
struct StageProcessContext {
    float sampleRate;
    int blockSize;
    int numChannels;
};

struct StageMeters {
    float gainReductionDb = 0.0f;
};

class ICompressorStage {
public:
    virtual ~ICompressorStage() = default;
    virtual void prepare(const StageProcessContext& ctx) = 0;
    virtual void reset() = 0;
    virtual void process(float** channels, int numChannels, int numSamples) = 0;
    virtual void setEnabled(bool enabled) = 0;
    virtual bool isEnabled() const = 0;
    virtual StageMeters getMeters() const = 0;
};
```

---

## Processing Order Pseudocode

```cpp
processBlock(buffer):
    applyInputCopyToDryBuffer(buffer)

    wetBuffer = buffer

    if oversampling > 1x:
        upsample(wetBuffer)

    for stage in chainOrder:
        if stage.enabled:
            stage.process(wetBuffer)

    if oversampling > 1x:
        downsample(wetBuffer)

    wetDryMix(dryBuffer, wetBuffer, dryWet)

    applyOutputGain(wetBuffer, outputGainDb)

    if safetyEnabled:
        safetyLimiter.process(wetBuffer)

    publishMeters(input, output, eachStageGR)

    output = wetBuffer
```

---

## UI Requirements

## UI-001: Panels

Render these blocks:

* GLOBAL
* METERS
* FET76
* OPTO2A
* VCA160
* VARIMU
* TUBE670

The “coming soon” panel is optional placeholder only.

## UI-002: Widgets

Use compact widgets matching prototype tone:

* ON/OFF toggle
* stepped enum selector in angle brackets
* value bars
* meter bars

## UI-003: Reorder UX

Each module must have one:

* drag handle, or
* move-left/move-right buttons

Must show visual current order.

---

## Acceptance Criteria

### AC-001

Plugin loads as VST3 and processes mono and stereo audio.

### AC-002

All 5 stages exist and can be enabled/disabled independently.

### AC-003

Stage order can be changed by user and affects sound.

### AC-004

Stage order persists after save/reload.

### AC-005

Dry/Wet, Output, Safety, Oversampling, and Tweak all function.

### AC-006

Input/output and per-stage GR meters update in real time.

### AC-007

No clicks on ordinary parameter changes.

### AC-008

No allocations or locks in process callback.

---

# JSON Handoff Schema

```json
{
  "project": {
    "name": "Pentagon",
    "pluginType": "multi-stage compressor",
    "formats": ["VST3"],
    "optionalFormats": ["AU", "CLAP"],
    "platforms": ["macOS", "Windows"],
    "channelSupport": ["mono", "stereo"]
  },
  "global": {
    "dryWet": {
      "type": "float",
      "range": [0.0, 1.0],
      "default": 0.71
    },
    "outputGainDb": {
      "type": "float",
      "range": [-24.0, 24.0],
      "default": 0.0
    },
    "safetyEnabled": {
      "type": "bool",
      "default": true
    },
    "oversampling": {
      "type": "enum",
      "values": ["1x", "2x", "4x"],
      "default": "1x"
    },
    "presetName": {
      "type": "string",
      "default": "VOICE RADIO"
    },
    "tweakMode": {
      "type": "enum",
      "values": ["Clean", "Balanced", "Aggressive", "Glue", "Vocal", "Loud"],
      "default": "Aggressive"
    }
  },
  "chain": {
    "reorderable": true,
    "defaultOrder": ["FET76", "OPTO2A", "VCA160", "VARIMU", "TUBE670"],
    "persistOrderInState": true
  },
  "stages": [
    {
      "id": "FET76",
      "enabledDefault": false,
      "parameters": {
        "enabled": { "type": "bool", "default": false },
        "inputGainDb": { "type": "float", "range": [-24.0, 24.0], "default": 0.0 },
        "outputGainDb": { "type": "float", "range": [-24.0, 24.0], "default": 0.0 },
        "attackUs": { "type": "float", "range": [20.0, 800.0], "default": 120.0 },
        "releaseMs": { "type": "float", "range": [20.0, 1200.0], "default": 220.0 },
        "thresholdDb": { "type": "float", "range": [-60.0, 0.0], "default": -24.0 },
        "saturationMode": { "type": "enum", "values": ["Off", "Clean", "Warm", "Drive"], "default": "Clean" }
      },
      "meter": "gainReductionDb",
      "character": "fast aggressive peak compressor"
    },
    {
      "id": "OPTO2A",
      "enabledDefault": true,
      "parameters": {
        "enabled": { "type": "bool", "default": true },
        "peakReduction": { "type": "float", "range": [0.0, 100.0], "default": 71.5 },
        "mode": { "type": "enum", "values": ["COMP", "LIMIT"], "default": "COMP" },
        "makeupGainDb": { "type": "float", "range": [-12.0, 24.0], "default": 0.0 },
        "hfEmphasis": { "type": "float", "range": [0.0, 100.0], "default": 0.0 },
        "saturationMode": { "type": "enum", "values": ["Off", "Clean", "Warm", "Tube"], "default": "Clean" }
      },
      "meter": "gainReductionDb",
      "character": "smooth optical compressor"
    },
    {
      "id": "VCA160",
      "enabledDefault": true,
      "parameters": {
        "enabled": { "type": "bool", "default": true },
        "thresholdDb": { "type": "float", "range": [-60.0, 0.0], "default": -28.6 },
        "overeasy": { "type": "bool", "default": true },
        "makeupGainDb": { "type": "float", "range": [-24.0, 24.0], "default": 0.0 },
        "saturationMode": { "type": "enum", "values": ["Off", "Clean", "Punch", "Warm"], "default": "Clean" }
      },
      "meter": "gainReductionDb",
      "character": "punchy VCA compressor"
    },
    {
      "id": "VARIMU",
      "enabledDefault": false,
      "parameters": {
        "enabled": { "type": "bool", "default": false },
        "thresholdDb": { "type": "float", "range": [-60.0, 0.0], "default": -22.0 },
        "attackMs": { "type": "float", "range": [0.1, 100.0], "default": 45.0 },
        "recovery": { "type": "enum", "values": ["Fast", "Medium", "Slow", "Auto", "0.4s"], "default": "0.4s" },
        "makeupGainDb": { "type": "float", "range": [-24.0, 24.0], "default": 0.0 },
        "saturationMode": { "type": "enum", "values": ["Off", "Clean", "Warm", "Thick"], "default": "Clean" }
      },
      "meter": "gainReductionDb",
      "character": "smooth glue vari-mu compressor"
    },
    {
      "id": "TUBE670",
      "enabledDefault": false,
      "parameters": {
        "enabled": { "type": "bool", "default": false },
        "thresholdDb": { "type": "float", "range": [-60.0, 0.0], "default": -18.0 },
        "timeConstant": { "type": "enum", "values": ["1", "2", "3", "4", "5", "6"], "default": "2" },
        "mode": { "type": "enum", "values": ["LR"], "default": "LR" },
        "makeupGainDb": { "type": "float", "range": [-24.0, 24.0], "default": 0.0 },
        "saturationMode": { "type": "enum", "values": ["Off", "Clean", "Tube", "Heavy"], "default": "Clean" }
      },
      "meter": "gainReductionDb",
      "character": "tube limiter / vari-mu style compressor"
    }
  ],
  "meters": {
    "globalMeters": ["inputPeakDbFs", "outputPeakDbFs"],
    "stageMeters": [
      "FET76.gainReductionDb",
      "OPTO2A.gainReductionDb",
      "VCA160.gainReductionDb",
      "VARIMU.gainReductionDb",
      "TUBE670.gainReductionDb"
    ]
  },
  "dsp": {
    "noAllocationsOnAudioThread": true,
    "noLocksOnAudioThread": true,
    "parameterSmoothing": true,
    "bypassCrossfadeMs": 10,
    "oversamplingAffectsWetPath": true,
    "latencyReportingRequired": true,
    "dryPathLatencyCompensationRequired": true
  },
  "safety": {
    "type": "output protection",
    "recommendedImplementation": ["soft clipper", "brickwall limiter"],
    "defaultCeilingDbFs": -1.0
  },
  "ui": {
    "style": {
      "background": "black",
      "text": "green monospace",
      "borders": "gold/yellow",
      "theme": "retro terminal"
    },
    "panels": ["GLOBAL", "METERS", "FET76", "OPTO2A", "VCA160", "VARIMU", "TUBE670"],
    "reorderControl": ["drag-drop", "move buttons"]
  },
  "state": {
    "serializeAllGlobals": true,
    "serializeAllStageParams": true,
    "serializeStageOrder": true,
    "serializeOversampling": true,
    "serializeTweakMode": true
  }
}
```

---

# Minimal Build Plan for Agent

## Phase 1

* create plugin shell
* implement parameter tree
* implement stage chain manager
* implement basic DSP for all 5 stages
* implement meters
* implement state serialization

## Phase 2

* implement reorderable UI
* connect reorder state to DSP chain
* add oversampling
* add safety stage
* add terminal-style skin

## Phase 3

* add factory presets
* refine tweak macro
* tune compressor voicings
* optimize CPU and automation behavior

---

# Recommended Parameter IDs

```text
global_drywet
global_output_gain
global_safety
global_oversampling
global_preset
global_tweak

fet_enable
fet_input_gain
fet_output_gain
fet_attack_us
fet_release_ms
fet_threshold
fet_sat

opto_enable
opto_peak_reduction
opto_mode
opto_makeup_gain
opto_hf_emphasis
opto_sat

vca_enable
vca_threshold
vca_overeasy
vca_makeup_gain
vca_sat

varimu_enable
varimu_threshold
varimu_attack_ms
varimu_recovery
varimu_makeup_gain
varimu_sat

tube670_enable
tube670_threshold
tube670_time_constant
tube670_mode
tube670_makeup_gain
tube670_sat

chain_slot_1
chain_slot_2
chain_slot_3
chain_slot_4
chain_slot_5
```


