# Changelog — jt-synth

## v0.1.0 — 2026-05-05

Initial release. Extracted from the [pocket-daw](https://github.com/poealone/pocket-daw) monorepo as part of the **desktop-alpha-1** ship.

Dual-oscillator wavetable synth with multimode filter, gate, built-in reverb + delay, and 28 EDM-focused factory presets.

Built against [pocketdaw-sdk](https://github.com/poealone/pocketdaw-sdk). See `manifest.json` for parameter list and UI metadata.

### Build

```
make            # Linux .so
make windows    # Windows .dll (needs MinGW + SDL2 mingw dev libs at /tmp/SDL2-2.30.10/x86_64-w64-mingw32)
make device     # aarch64 .so for RG35XX
make deploy     # copy artefacts into ../pocket-daw/plugins/synths/jt-synth/
```
