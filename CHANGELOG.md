# Changelog

## 1.1.0

- Added a transparent fullscreen FFT toggle in the spectrum corner. The FFT fills the plugin window while the right button rail stays available, and the same corner or `Escape` restores the normal layout.
- The toggle uses a square outline cue in both states.

## 1.0.10

- Low-frequency spectrum bins are averaged with a log-spaced taper below 500 Hz, so the curve reads as a line instead of a staircase where several display bins collapse onto one FFT bin.
- The waterfall softens its rows with a 3-tap horizontal blur and blends each row with the previous one, removing the hard block edges between log bins.

## 1.0.11

- Clarified the gain-staging menu item as `Reference lines`.
- Split UI snapshot generation: the layout QA image is written as `qa-lite-layout.png`, while the website Lite preview is generated separately as `ui-preview-lite.png` from a clean, locked-out processor state.
