# Changelog

## 1.2.0

- Added a track report: drop an audio file onto the window (or
  `OPTIONS -> Export -> Track report`) and the whole file is analysed offline
  and saved as a one-page PDF - integrated LUFS, LRA, PLR, true peak, the
  short-term loudness timeline against the target with every passage above it,
  the average spectrum with the band balance, and a table of the longest
  excursions. Rendered as vector PDF, no external libraries.
- Added a per-band stereo field under the spectrum (PRO, `OPTIONS -> Display ->
  Stereo field by band`). The strip height is side/mid energy in dB, so the
  bottom is mono and the 0 dB line means the band is as wide as it is mono; the
  outline colour is correlation, from red on anti-phase bands to cyan on
  mono-compatible ones. Hovering the spectrum reports correlation and side/mid
  for the band under the cursor. Project state is version 18.

## 1.1.0

- Added a transparent fullscreen FFT toggle in the spectrum corner. The FFT fills the plugin window while the right button rail stays available, and the same corner or `Escape` restores the normal layout.
- The toggle uses a square outline cue in both states.

## 1.0.10

- Low-frequency spectrum bins are averaged with a log-spaced taper below 500 Hz, so the curve reads as a line instead of a staircase where several display bins collapse onto one FFT bin.
- The waterfall softens its rows with a 3-tap horizontal blur and blends each row with the previous one, removing the hard block edges between log bins.

## 1.0.11

- Clarified the gain-staging menu item as `Reference lines`.
- Split UI snapshot generation: the layout QA image is written as `qa-lite-layout.png`, while the website Lite preview is generated separately as `ui-preview-lite.png` from a clean, locked-out processor state.
