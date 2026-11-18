# LudensScope

[![build](https://github.com/ltillegal/ludensscope/actions/workflows/build.yml/badge.svg)](https://github.com/ltillegal/ludensscope/actions/workflows/build.yml)

[Русский](README.md) · **English** · [❤ Donate](https://www.paypal.com/ncp/payment/MAMKZM5TTVN5U?locale.x=en_US&country.x=US)

A minimalist stereo analyser in **VST3, AU, AAX, CLAP** and standalone formats: spectrum, loudness
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
| Reference: loading, analysis, one file, ORIG/REF, player, drag and drop | `Source/dsp/ReferenceAnalysis.h`, `Source/PluginProcessor.*` |
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
   Shift/Alt-click a zone to solo that band (only the band reaches the
   output; the analysers still see the full mix); repeat to turn it off.

   The `OPTIONS` button opens a menu with `FFT`, `Display`, `Reference` and
   `Export` submenus: window size 2048–16384, window function
   (Hann / Blackman-Harris / Flat-top), smoothing speed (Fast / Medium /
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
   window). The averaged curve is drawn in grey; a full-width reference strip at
   the bottom holds the waveform, the loop region (drag the wave to select, drag
   its edges separately, double-click for the whole file), the playhead,
   play/`LOOP`/`REF-MIX`/`MATCH`/`MIRROR`, time, name and LUFS. There is now
   **one reference file**. The A–D slots became an **ORIG / REF** source switch:
   `ORIG` feeds the sound and every indicator from the DAW, `REF` feeds them
   (spectrum, LUFS, true peak, VU, oscilloscope, waterfall, correlation) from the
   reference player. `MATCH` continuously level-matches the reference to the
   mix's integrated LUFS; `MIRROR` makes the reference follow the DAW transport.
   The path, curve, LUFS and loop are stored in the project; on project open the
   file is reloaded, and if it is missing the curve remains for comparison.
   Files longer than 10 minutes are truncated.

   `FREEZE` stops the frame; `RESET` clears peaks, loudness, the selected
   snapshot, the reference and the counters; `UNDO` restores the state from
   before the last reset (integrated loudness, LRA, snapshot, reference and
   counters). `OPTIONS → Export` also exports snapshots to CSV (bin, frequency,
   levels A–D).

   The project state stores the snapshots, all FFT settings, the view (guide
   genre and visibility, M/S curve, peak markers, difference shading, LUFS
   target, SPAN range, waterfall speed, scope window/gain, target curve) and the
   reference metadata.

Hotkeys: `A` — REF/MIX, `F` — freeze, `Esc` — clear the band solo.
Guides: `Shift`/`Alt` + click on a zone toggles band solo; a plain click on
the graph never filters the host output. Space is not intercepted — it belongs
to the host transport. The plugin also
works on mono tracks (mono/mono bus): loudness and metering use one channel;
duplication is only for the spectrum/vector analysis.

The `THEME` button in the top-right switches seven interface palettes:
**Blue** (current), **Dark**, **Light**, **Magenta**, **Green**, **Red**,
**Orange**. The choice is global: it is stored in LudensScope's settings and
applied to every open instance; the light theme re-tints the
logo and curves automatically, and the waterfall follows the palette. The
right-rail buttons, `THEME`, ORIG/REF and the transport scale with the
window (about 0.9–1.35×). The `THEME` menu also offers **UI scale**:
Auto / 100% / 125% / 150%, scaling fonts, controls and the window.

True peak uses an approximate detector (12-tap windowed sinc); expect a
fraction of a dB of difference from the BS.1770-4 Annex 2 reference filter.

### Gain staging and VU

`OPTIONS → Display → Gain staging` shows horizontal reference lines in the spectrum and
oscilloscope: 0 dBFS, -6, -12 and 0 VU; the 0 VU line is highlighted. The
calibration switches between **0 VU = -18 dBFS (EBU)** and **-20 dBFS
(SMPTE)**. The bottom row carries an 80s cassette-deck style stereo VU: two
separate L/R needle indicators in a rectangular window; needles rest left.

The REF curve shows the power average plus a **p10-p90** band (the
reference's variation over time); `OPTIONS → Reference → Suggested EQ (REF)` lists up to
four broad `CUT/BOOST` hints from the difference of the average spectra
(read-only, no processing).

The VU follows IEC 60268-17 / ANSI C16.5: 0 VU = +4 dBu = 1.228 V RMS at
1 kHz, -20..+3 VU scale. The pointer deflection is proportional to the
**voltage**, so the scale is non-linear: 0 VU sits at ~68.5% of the arc and
the 0..+3 zone is red. Ballistics are second-order in the voltage domain:
300 ms second-order + 40 ms input smoothing (≈400 ms to 99%, zeta ~0.8, omega_n ~16.7 rad/s).
In the digital domain 0 VU is the alignment level -18 dBFS (EBU R68) or
-20 dBFS (SMPTE RP 155); the VU reads average loudness, not peaks - use
TRUE PEAK for peaks.

## Install from a release

- Linux: `sudo apt install ./LudensScope-linux.deb` — VST3 to `/usr/lib/vst3`,
  CLAP to `/usr/lib/clap`, standalone app in the menu (`ludensscope`).
- macOS: `LudensScope-macos.pkg` — VST3/AU/CLAP to `/Library/Audio/Plug-Ins`,
  AAX to `/Library/Application Support/Avid/Audio/Plug-Ins`, the app to `/Applications`.
- Windows: `LudensScope-windows.exe` — VST3 to `Common Files\VST3`, CLAP to
  `Common Files\CLAP`, AAX to `Common Files\Avid\Audio\Plug-Ins`, the standalone
  app to `Program Files\LudensScope`.
- [Komi Store](https://komistore.app/) picks the release up from the installable
  assets (`.deb` / `.pkg` / `.exe`) automatically.

AAX is built for macOS and Windows but ships **unsigned by PACE**: Pro Tools
only loads a signed AAX (Avid Developer/PACE required). CLAP is an open format
(MIT clap-juce-extensions wrapper); the standalone is a regular app with the
same interface.

## Updates

`FFT` → `Check for updates...` queries the latest GitHub release. If a newer
version exists, the plugin downloads the installer for the current OS to
`~/Downloads`, verifies its SHA-256 against `SHA256SUMS.txt` from the same
release and reveals the folder. The plugin never replaces its own loaded binary:
quit the DAW and run the downloaded installer.

Releases are built by `.github/workflows/release.yml` from a `vX.Y.Z` tag:
Linux — `.deb`, macOS — `.pkg` (VST3 + AU), Windows — `.exe`, plus
`SHA256SUMS.txt`. There are no zip assets. The build version comes from
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

[![Donate](https://img.shields.io/badge/Donate-PayPal-00457C?logo=paypal&logoColor=white)](https://www.paypal.com/ncp/payment/MAMKZM5TTVN5U?locale.x=en_US&country.x=US)

PayPal: [Donate](https://www.paypal.com/ncp/payment/MAMKZM5TTVN5U?locale.x=en_US&country.x=US)
