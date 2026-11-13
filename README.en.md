# LudensScope

[![build](https://github.com/ltillegal/ludensscope/actions/workflows/build.yml/badge.svg)](https://github.com/ltillegal/ludensscope/actions/workflows/build.yml)

[Русский](README.md) · **English**

A minimalist stereo analyser in **VST3 and AU** formats: spectrum, loudness
to ITU-R BS.1770-4, true peak, vectorscope, oscilloscope and reference.

Free, no subscriptions, no telemetry. The project state stores FFT snapshots,
analyser settings, the UI state and the reference metadata. There are no
general plugin presets — only named target curves.

## What's inside

| Module | Files |
|---|---|
| LUFS (M/S/I, gating), K-weighting, LRA | `Source/dsp/LoudnessMeter.*` |
| Spectrum: FFT 2048–16384, Hann / Blackman-Harris / flat-top, log scale 20 Hz – 20 kHz, inspector, averaging, target curves | `Source/dsp/SpectrumAnalyzer.*`, `Source/ui/SpectrumView.h` |
| True peak: 4x oversampling, polyphase FIR | `Source/dsp/TruePeakDetector.h` |
| PLR and clip counter | `Source/PluginProcessor.*`, `Source/PluginEditor.cpp` |
| UI: spectrum, waterfall, meters, goniometer, oscilloscope, guides | `Source/ui/*` |
| Reference: loading, analysis, two slots, player, drag and drop | `Source/dsp/ReferenceAnalysis.h`, `Source/PluginProcessor.*` |
| Update check | `Source/ui/UpdateChecker.h` |

The audio thread only filters and measures energy; FFT and drawing run on the
UI thread (data goes through a lock-free `AbstractFifo`). No locks in
`processBlock`.

## Building (Linux)

```bash
cmake --preset default
cmake --build --preset default -j$(nproc)
```

or without presets:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

JUCE (8.0.15) is fetched automatically with `FetchContent`. The built VST3 is
copied to `~/.vst3/LudensScope.vst3`.

Optional calibration checks:

```bash
cmake --preset dev
cmake --build --preset dev -j$(nproc)

# LUFS (BS.1770-4) + true peak + LRA: 13 checks
./build/LudensScopeLoudnessCalibration

# spectrum calibration: a -6 dBFS sine should read about -6 dBFS
./build/LudensScopeCalibration_artefacts/Release/LudensScopeCalibration

# render the UI to PNG without a DAW (editor smoke test)
./build/LudensScopeUiSnapshot_artefacts/Release/LudensScopeUiSnapshot
# -> docs/ui-preview.png (or the path passed as the first argument)
```

## Usage

1. Put the plugin on the master bus or a group.
2. Spectrum: log axis, dBFS scale, the yellow line is peak hold. Mouse wheel
   over the spectrum zooms octaves around the cursor; double-click resets to
   the full range (20 Hz – 20 kHz). The `SPAN` button cycles 10/8/6/4/2/1
   octaves; the waterfall follows the visible range. Hover to read frequency,
   note, level, peak and the Q of the nearest peak (from its −3 dB width).
   Below is the waterfall (~3/7/14 s of history); `FREEZE` stops it. Drag the
   divider to resize the waterfall, double-click to reset. The HUD pills at the
   top show the LOW/MID/HIGH energy balance. `GUIDES` opens a menu — Off or a
   genre zone set (General / EDM / Hip-Hop / Rock / Acoustic / Pop / Metal /
   Jazz / Orchestral / Lo-Fi / R&B): a hint of where instruments usually live.
   Click a zone to solo that band (only the band reaches the output; the
   analysers still see the full mix); click again to turn it off.

   The `FFT` button opens the analyser settings: window size 2048–16384, window
   function (Hann / Blackman-Harris / Flat-top), smoothing speed (Fast / Medium /
   Slow), tilt +3 or +4.5 dB/oct, 1/1, 1/3 or 1/6 octave smoothing, FFT overlap
   2x/4x and the C1–C8 note grid. The three strongest peaks are labelled
   automatically; turn them off with Peak markers. The same menu has the
   difference shading with a selectable second curve (next snapshot, REF FILE or
   live), the M/S side curve and PNG export of the whole view. Curve presets:
   `Save current curve` stores the averaged spectrum as a named target curve
   (globally, in `~/.config/LudensScope/curve-presets.json`), `Load curve` draws
   it as an amber dashed line with a TARGET chip, `Delete curve` removes it. The
   selected target curve is stored in the project.
3. LOUDNESS: momentary (400 ms), short-term (3 s), integrated (gated at
   −70 LUFS / −10 LU). Click `TGT` in the header to choose a target
   (−14/−16/−18/−23); a mark appears on the integrated bar with the deviation in
   LU below it. The short-term history for the last 60 seconds is drawn with the
   target line.
4. TRUE PEAK — 4x oversampling, dBTP. Red above −1 dBTP. LRA — loudness range
   (EBU 3342), PLR — peak-to-loudness, CLIP — the number of samples ≥ 0 dBFS
   plus intersample overs.
5. STEREO IMAGE — goniometer with a trail; OSCILLOSCOPE — zero-crossing
   triggered. Mouse wheel over the scope changes the time window (up to ~2.7 s),
   Shift+wheel the vertical gain, double-click resets. The badge shows PEAK /
   RMS / CREST / LUFS over the visible window. `MONO` analyses the channel sum
   to check mono compatibility.
6. FFT snapshots: click the `A`–`D` letter in the spectrum header to select a
   slot; `CAPTURE` stores the averaged spectrum in it. Filled slots are drawn as
   coloured dashed lines; the inspector shows the level and the delta to the
   selected slot.

   Reference: `REF FILE` loads wav/aiff/flac (or drop a file onto the plugin
   window). The averaged curve is drawn in grey; a transport appears on the
   spectrum (play/stop, `LOOP`, `REF/MIX`, `MATCH`, `MIRROR`, time) with a seek
   bar: click to seek, drag to select a loop range, drag its edges separately;
   the file waveform is drawn on the bar. There are **four reference slots A–D**
   — the `REF` row at the bottom of the right rail (not to be confused with
   `SNAP A–D`, which are spectrum snapshots); switching is instant (audio is
   cached in memory), and the path, curve, LUFS and loop are stored in the
   project. `MATCH` continuously level-matches the reference to the mix's
   integrated LUFS; `MIRROR` makes the reference follow the DAW transport (FREE
   mode is seek/loop). An empty slot shows `REF X EMPTY — USE REF FILE` on the
   spectrum. The reference gain
   is matched to the current integrated loudness of the mix. On project open the
   file is reloaded; if it is missing, the curve remains for comparison. Files
   longer than 10 minutes are truncated.

   `FREEZE` stops the frame; `RESET` clears peaks, loudness, the selected
   snapshot, the reference and the counters; `UNDO` restores the state from
   before the last reset (integrated loudness, LRA, snapshot, reference and
   counters). The `FFT` menu also exports snapshots to CSV (bin, frequency,
   levels A–D).

   The project state stores the snapshots, all FFT settings, the view (guide
   genre and visibility, M/S curve, peak markers, difference shading, LUFS
   target, SPAN range, waterfall speed, scope window/gain, target curve) and the
   reference metadata.

Hotkeys: `Space` — reference play/stop, `A` — REF/MIX, `F` — freeze,
`Esc` — clear the band solo.

The `THEME` button in the top-right switches seven interface palettes:
**Blue** (current), **Dark**, **Light**, **Magenta**, **Green**, **Red**,
**Orange**. The choice is stored in the project; the light theme re-tints the
logo and curves automatically, and the waterfall follows the palette.

## Updates

`FFT` → `Check for updates...` queries the latest GitHub release. If a newer
version exists, the plugin downloads the package for the current OS to
`~/Downloads`, verifies its SHA-256 against `SHA256SUMS.txt` from the same
release and reveals the folder. The plugin never replaces its own loaded binary:
quit the DAW, unzip the archive and replace the VST3/AU.

Releases are built by `.github/workflows/release.yml` from a `vX.Y.Z` tag:
macOS — VST3 + AU, Linux and Windows — VST3. The build version comes from
`-DLUDENSCOPE_VERSION`, so the tag and the version must match. The update check
only makes a network request when you ask for it. The binaries are unsigned and
not notarised — on macOS you may need to clear the quarantine flag.

## macOS + Cubase / Logic

VST3 works in Cubase, AU in Logic and GarageBand; both are built for Intel and
Apple Silicon. The plugin must be built **on macOS** — cross-compiling from
Linux is not supported. The sources are platform-independent (plain C++ + JUCE);
there is no Linux-specific code.

```bash
xcode-select --install   # if the Command Line Tools are missing

# everything: configure + build + ad-hoc signing + install
./scripts/build-macos.sh
```

Manually:

```bash
cmake --preset macos-universal
cmake --build --preset macos-universal -j$(sysctl -n hw.ncpu)

# local ad-hoc signature (required on arm64)
codesign --force --deep --sign - \
  build/LudensScope_artefacts/Release/VST3/LudensScope.vst3
codesign --force --deep --sign - \
  build/LudensScope_artefacts/Release/AU/LudensScope.component
```

`COPY_PLUGIN_AFTER_BUILD` installs the VST3 into
`~/Library/Audio/Plug-Ins/VST3/` and the AU into
`~/Library/Audio/Plug-Ins/Components/` (system-wide:
`/Library/Audio/Plug-Ins/...`).

In Cubase: `Studio → VST Plug-in Manager → Rescan`, or just restart Cubase. The
plugin appears under `Analyzer`. For Logic/GarageBand restart the app; check the
AU with `auval -v aufx LDSC LDNS`.

Notes:

- The universal binary (`arm64;x86_64`) is needed for both Apple Silicon and
  Intel.
- For distribution to other people use a Developer ID + notarisation. Ad-hoc
  signing is enough for personal use.
- VST3 and AUv2 are built; AAX and AUv3 (App Extension) are not.
- The macOS build is checked in CI but has not been tested on real hardware.

## Limitations

- Stereo only (2 in / 2 out), VST3 and AUv2 formats.
- No general plugin presets; target curves are global presets.
- AAX and AUv3 are not built.
- Updates only work through a published GitHub Release; there is no automatic
  installation because the host keeps the binary loaded until the DAW restarts.

## Support the project

If LudensScope saves you time, you can support development via PayPal:

[![Donate](https://img.shields.io/badge/Donate-PayPal-00457C?logo=paypal&logoColor=white)](https://www.paypal.com/donate/?business=0eskimo0%40gmail.com&item_name=LudensScope&currency_code=USD)

PayPal: **0eskimo0@gmail.com**
