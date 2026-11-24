# ArchiveTV

A cute little TV for your Miyoo Mini. Browse, stream and download videos from the
[Internet Archive](https://archive.org) on Onion OS.

![Hello screen](docs/hello.png) ![Guide](docs/guide.png) ![Downloading](docs/download.png)

- **Search** videos and collections, with recent searches kept
- **Guide**: hand-picked shelves of cartoons, 80s & 90s anime, movies and more (edit `guide.txt`)
- **Watch now**: starts playing after a few percent, keeps downloading as you watch
- **Download queue** with pause, resume and partial downloads that survive Wi-Fi drops
- **Saved** shows and searches, **playlists**, resume where you stopped
- Episodes of a show **folded into one row**, sorting (episode order, newest, oldest...)
- Optional **turn off when downloads finish**: queue a bunch and go to bed
- Four themes (Night by default), scanlines, menu music and sounds

## Devices

One build for the whole SSD202D Miyoo family on **Onion OS 4.2+**, and in beta on **spruceOS** and **sprigUI**:

| Device | Screen |
| --- | --- |
| Miyoo Mini v1–v3, Mini Plus | 640×480 |
| Miyoo Mini v4, Mini Flip | 752×560 |

Browsing and downloading need Wi-Fi (Mini Plus, Mini Flip). Models without Wi-Fi play
videos copied into `App/ArchiveTV/videos`.

## Install

Download `ArchiveTV-vX.Y.zip` from [Releases](../../releases), copy its `App` folder to the
root of your SD card (merge folders), and open **ArchiveTV** from Apps.
It's also listed in [Mixtape](https://github.com/snyderman3000/mixtape).

Lists show a small preview picture of the selected item (Settings › Thumbnails).

From 1.4 on it can update itself: **Settings › Update ArchiveTV** (needs Wi-Fi). Only the changed
files are downloaded, each one is checked, and your videos, playlists and settings are never touched.

## Controls

| Button | Menus | Playing |
| --- | --- | --- |
| D-pad | move, left/right = page | left/right ±10 s, up/down ±1 min |
| A | open / menu | pause |
| B | back | stop |
| START | do it now (play, search, open) | pause |
| Y | save to Saved | show/hide time bar |
| X | tap: delete / sort; hold: scroll long titles | screen off/on |
| L1 / R1, L2 / R2 | switch tab | L1/R1 ±1 min, L2/R2 ±5 min |
| SELECT | change theme | start over |
| MENU | turn ArchiveTV off | stop |

## How it works

`archivetv` is a single C program on SDL 1.2. It plays video with FFmpeg 2.4 (LGPL build in
`App/ArchiveTV/libs`, made by `build-ffmpeg.sh`) and fetches everything with the system's `curl`
(Onion OS or spruceOS).

### Building

Needs `clang`, `lld` and `llvm` tools; no Miyoo SDK.

```sh
sh build-ffmpeg.sh /path/to/miyoomini-toolchain   # FFmpeg libs -> App/ArchiveTV/libs
mkdir -p libs && cp App/ArchiveTV/libs/* libs/    # build.sh links against these
sh build.sh                                       # -> build/archivetv
cp build/archivetv App/ArchiveTV/
```
For systems without curl (sprigUI) the release zip also carries a static curl in `App/ArchiveTV/bin`
(the same build spruceOS ships in `spruce/bin/curl`).
The toolchain is the [pre-built Miyoo Mini toolchain](https://github.com/shauninman/miyoomini-toolchain-buildroot/releases).

### Releasing an update

The app's updater reads `update.txt` on the `miyoo-builds` branch: the version, plus each file's CRC-32
and size, taken from one fixed commit of that branch. Bump `src/version.h`, build, put together the full
`App/ArchiveTV` folder (with `archivetv`, `libs/` and `bin/curl`), then

```sh
tools/publish-update.sh path/to/App/ArchiveTV
```

That push reaches every 1.4+ install at its next start, so do it together with the GitHub release.

## Credits

- Made by [@0kCyb0rg](https://x.com/0kCyb0rg) · [okcyborg.com](https://okcyborg.com)
- Music: "Select a Level" by @0kCyb0rg · menu sounds by @0kCyb0rg
- Font: [m5x7](https://managore.itch.io/m5x7) by Daniel Linssen
- Video playback: [FFmpeg](https://ffmpeg.org) 2.4 (LGPL) · downloads: [curl](https://curl.se)
- Videos: the Internet Archive. Please [donate](https://archive.org/donate).

Code is MIT licensed; the music and sounds are not (see [LICENSE](LICENSE)).
