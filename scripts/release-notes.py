#!/usr/bin/env python3
"""Compose GitHub release notes from the CHANGELOG section for a tag.

Usage: python3 scripts/release-notes.py v1.1.0 [> release-notes.md]

The CHANGELOG owns the highlights; this script adds the download table, the
short product blurb and the guide links, so every release page looks the same
even when the notes are written in a hurry.
"""

from __future__ import annotations

import pathlib
import re
import sys

SITE = "https://ludensscope.com"
REPO = "https://github.com/ltillegal/ludensscope"

DOWNLOAD_TABLE = """### Download

| Platform | File |
| --- | --- |
| Windows 10/11 (64-bit) | `LudensScope-windows.exe` — VST3, CLAP, AAX, standalone |
| macOS (universal, Apple Silicon native) | `LudensScope-macos.pkg` — VST3, AU, CLAP, AAX, standalone |
| Linux x86_64 | `LudensScope-linux.deb` — VST3, CLAP, standalone |

Every file is listed in `SHA256SUMS.txt`. AAX ships unsigned by PACE, so Pro Tools will not load it without Avid/PACE signing — VST3, AU, CLAP and the standalone app are unaffected.
"""

PRODUCT = f"""### What LudensScope is

A stereo analyser for mixing and mastering: 2048–16384-point FFT spectrum and waterfall, ITU-R BS.1770-4 loudness (momentary, short-term, integrated, LRA, PLR, true peak), goniometer, oscilloscope, vectorscope and a voltage-proportional VU.

**Lite is free** — the whole metering set, no time limit, no account, no telemetry. **PRO (€19)** adds the reference workflow (four slots, p10–p90 band, level matching, ORIG/REF monitoring, EQ hints), A–D snapshots, gain-staging guides and all seven palettes.

Website: {SITE} · Guides: [LUFS meter]({SITE}/free-lufs-meter/), [spectrum]({SITE}/free-spectrum-analyzer/), [gain staging]({SITE}/gain-staging/)
"""


def changelog_section(text: str, version: str) -> str:
    pattern = rf"^##\s+{re.escape(version)}\s*$(.*?)(?=^##\s|\Z)"
    match = re.search(pattern, text, flags=re.MULTILINE | re.DOTALL)
    if not match:
        return ""
    return match.group(1).strip()


def main() -> int:
    tag = (sys.argv[1] if len(sys.argv) > 1 else "").strip()
    if not tag:
        print("usage: release-notes.py <tag>", file=sys.stderr)
        return 2

    version = tag.lstrip("v")
    changelog = pathlib.Path("CHANGELOG.md")
    text = changelog.read_text(encoding="utf-8") if changelog.exists() else ""
    highlights = changelog_section(text, version)

    if not highlights:
        highlights = f"Release {version}. See the changelog for details."

    highlights = "### Highlights\n\n" + highlights

    parts = [
        highlights,
        DOWNLOAD_TABLE,
        PRODUCT,
        f"**Full Changelog**: {REPO}/compare/{previous_tag(text, version)}...{tag}",
    ]
    print("\n\n".join(parts).strip())
    return 0


def previous_tag(text: str, version: str) -> str:
    """Tag of the release right before this one, taken from the changelog order."""
    headings = re.findall(r"^##\s+(\S+)\s*$", text, flags=re.MULTILINE)
    try:
        index = headings.index(version)
    except ValueError:
        return "HEAD"
    return "v" + headings[index + 1] if index + 1 < len(headings) else "HEAD"


if __name__ == "__main__":
    raise SystemExit(main())
