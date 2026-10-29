# RtMidi (bundled)

Unmodified copy of `RtMidi.h`, `RtMidi.cpp`, and `LICENSE` from RtMidi 6.0.0
(https://github.com/thestk/rtmidi, tag `6.0.0`).

- Source archive: `https://github.com/thestk/rtmidi/archive/refs/tags/6.0.0.tar.gz`
- SHA-256 of that archive: `ef7bcda27fee6936b651c29ebe9544c74959d0b1583b716ce80a1c6fea7617f0`
- License: MIT-style (see `LICENSE`)

Bundled so the app has no RtMidi package dependency (most distros don't ship it).
Built as a static library by the top-level `CMakeLists.txt` with the platform backend
(ALSA on Linux, CoreMIDI on macOS, Windows MM on Windows).
