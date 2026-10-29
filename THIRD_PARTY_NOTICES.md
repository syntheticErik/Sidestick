# Third-party notices

Sidestick is licensed under the GNU General Public License, version 3 or later (see `LICENSE`).
It uses the following third-party software, each under its own licence.

## Included in the source

| Component | Licence | Notes |
|---|---|---|
| [RtMidi](https://github.com/thestk/rtmidi) 6.0.0 | MIT-style | Unmodified copy in `third_party/rtmidi/`, with its `LICENSE` |

## Bundled in the AppImage

The AppImage includes these libraries, unmodified, so it can run on systems that don't have them. Native builds (the SlackBuild, or building from source) use the system's copies instead.

| Component | Licence | Source |
|---|---|---|
| [Qt](https://www.qt.io/) 6.8 (Core, Gui, Widgets, Multimedia, DBus, and platform plugins) | LGPL-3.0 | https://download.qt.io/official_releases/qt/ |
| [FFmpeg](https://ffmpeg.org/) 7.1 (via Qt Multimedia) | LGPL-2.1 or later | https://ffmpeg.org/download.html |
| [libsndfile](https://libsndfile.github.io/libsndfile/) | LGPL-2.1 or later | https://github.com/libsndfile/libsndfile |
| [libsamplerate](https://libsndfile.github.io/libsamplerate/) | BSD-2-Clause | https://github.com/libsndfile/libsamplerate |

The AppImage also contains supporting system libraries from Ubuntu 22.04 (such as the XCB, D-Bus, and PulseAudio client libraries), each under its own licence. Their sources are available from Ubuntu's archive: https://packages.ubuntu.com/jammy/

The LGPL-licensed libraries are dynamically linked and stored as separate files inside the AppImage, so they can be replaced: extract the AppImage with `--appimage-extract`, swap the library in `squashfs-root/usr/lib/`, and run `squashfs-root/AppRun`.
