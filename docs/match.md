# LudensMatch

Reference-matching EQ. A separate product from LudensScope: its own plug-in
identity (`LDMT` / `LDNS`), its own licence keys and its own settings folder.

## What it does

The editor analyses a reference file offline (FFT plus loudness), compares its
balance with the incoming mix in eight log bands, and feeds the difference to
the audio thread as band gains. Correction is normalised by the mean band
difference, so matching cannot make the material quieter by itself; the LUFS
readout is informational only.

## Editions

| | Lite (free) | FULL (29 EUR) |
|---|---|---|
| Spectrum, reference load, correction curve | yes | yes |
| Audio processing | bypass, Output trim only | yes |
| Amount / Mix | locked | yes |

The edition is read at construction time, so a bounce or a session restored
with the editor closed still corrects. Keys are offline (`LS1|…`, RSA-2048,
public key in `Source/Licence.h`); the seller tool is `scripts/make-licence
--product LudensMatch`.

## Licence file

| Platform | Path |
|---|---|
| macOS | `~/Library/Application Support/LudensMatch/licence.txt` |
| Windows | `%APPDATA%\LudensMatch\licence.txt` |
| Linux | `~/.config/LudensMatch/licence.txt` |

`licence-machines.txt` appears next to it after the first successful start: keys
issued after 2026-11-19 are activated on up to two machines. LudensScope keeps
its older `~/Library/LudensScope` folder until it moves on its own schedule —
the two products do not share a directory.

If the editor shows `LITE - NO KEY`, `LITE - BAD KEY` or `LITE - MACHINE
LIMIT`, hovering that label shows the exact path the plug-in looked at and the
reason it gave up.

## Build

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
          -DLUDENSCOPE_BUILD_MATCH=ON -DLUDENSCOPE_COPY_PLUGIN=OFF
    cmake --build build --config Release --target LudensMatch_VST3 LudensMatch_AU LudensMatch_Standalone --parallel

Targets: `LudensMatch_Standalone`, `LudensMatch_VST3`, `LudensMatch_AU`,
`LudensMatch_CLAP`. Artefacts land in
`build/LudensMatch_artefacts/Release/`. The preview in `docs/match-preview.png`
is rendered by the `LudensMatchSnapshot` target (`LUDENSCOPE_MATCH_KEY` in the
environment renders the FULL state).
