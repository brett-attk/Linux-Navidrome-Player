# Linux Navidrome Player

A desktop music player for Linux that connects to a [Navidrome](https://www.navidrome.org/) music server (or another server that speaks the same common “Subsonic” web API that many self-hosted libraries use).

Browse your library, play albums and queues, favorite and rate songs, and keep your server’s play history up to date while you listen.

Built with GTK 4 and libadwaita (the usual toolkit for modern GNOME-style Linux apps), in C++.

**License:** [MIT](LICENSE)

---

## Requirements

- A Linux desktop (Wayland or X11)
- CMake **3.20+**
- A C++20 compiler (`g++` or `clang++`)
- Development packages for GTK 4, libadwaita, GStreamer (audio playback), libsoup 3 (HTTP), and nlohmann-json
- A Navidrome server you can reach on your network (or another Subsonic-compatible server)

---

## 1. Install dependencies

### Debian / Ubuntu

```bash
sudo apt update
sudo apt install \
  build-essential \
  cmake \
  pkg-config \
  libgtk-4-dev \
  libadwaita-1-dev \
  libgstreamer1.0-dev \
  libgstreamer-plugins-base1.0-dev \
  gstreamer1.0-plugins-good \
  gstreamer1.0-plugins-bad \
  gstreamer1.0-libav \
  libsoup-3.0-dev \
  nlohmann-json3-dev
```

### Fedora

```bash
sudo dnf install \
  gcc-c++ \
  cmake \
  pkgconf-pkg-config \
  gtk4-devel \
  libadwaita-devel \
  gstreamer1-devel \
  gstreamer1-plugins-base-devel \
  gstreamer1-plugins-good \
  gstreamer1-plugins-bad-free \
  gstreamer1-plugin-libav \
  libsoup3-devel \
  json-devel
```

### Arch Linux

```bash
sudo pacman -S --needed \
  base-devel \
  cmake \
  gtk4 \
  libadwaita \
  gstreamer \
  gst-plugins-base \
  gst-plugins-good \
  gst-plugins-bad \
  gst-libav \
  libsoup3 \
  nlohmann-json
```

---

## 2. Get the source

```bash
git clone https://github.com/brett-attk/Linux-Navidrome-Player.git
cd Linux-Navidrome-Player
```

Or, if you already have this directory:

```bash
cd /path/to/Linux-Navidrome-Player
```

---

## 3. Configure

Create a Release build directory:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

For a Debug build instead:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
```

---

## 4. Build

```bash
cmake --build build -j"$(nproc)"
```

| Binary | Path |
|--------|------|
| App | `build/radio-player` |
| Unit tests | `build/radio-tests` |

---

## 5. Run

```bash
./build/radio-player
```

### First-time connection

1. On launch, the **Connect to Navidrome** dialog appears (or click **Server** in the header bar).
2. Enter:
   - **Server URL** — for example `http://192.168.1.10:4533` (Navidrome’s default port is **4533**)
   - **Username**
   - **Password**
3. Or click **Discover on LAN** to search your local network for a server automatically.
4. Click **Connect**.

Saved settings live on your machine only:

| What | Where |
|------|--------|
| Server login | `~/.config/radio-player/config.json` |
| Last queue / playback position | `~/.config/radio-player/session.json` |
| Downloaded album art | `~/.cache/radio-player/covers/` |

---

## How listening is recorded on the server (“scrobbling”)

Just starting to stream a file is not enough for Navidrome to treat a song as “played.” The player must also **tell the server** that you listened.

That report is traditionally called a **scrobble** (the same idea as Last.fm “scrobbling”): a small message from the player saying “this user listened to this track.”

This app does that in two steps:

1. When a track **starts**, it notifies the server that it is now playing (does not usually increase the play count).
2. After you have listened long enough — about **30 seconds** or **half the song**, whichever comes first — or when the track ends / you skip after that point, it sends a **submitted** listen. That is what updates **play counts**, **Recently Played**, and similar Home sections on the server.

So if play counts look wrong in another client, check that this player stayed open long enough on the track for a submitted listen to be sent.

---

## 6. Run tests

```bash
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Or run the test binary directly:

```bash
./build/radio-tests
```

---

## 7. Install so it appears with your other apps

To put **Radio Player** in your desktop app menu (Activities / Application launcher) with its icon, install the binary, `.desktop` entry, and icon theme files under your user prefix.

Set the install prefix **when configuring** (so the menu entry’s `Exec=` path is correct), then build and install:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build -j"$(nproc)"
cmake --install build
```

That installs:

| What | Where |
|------|--------|
| Binary | `~/.local/bin/radio-player` |
| App menu entry | `~/.local/share/applications/ai.workspace.radioplayer.desktop` |
| Icons | `~/.local/share/icons/hicolor/*/apps/ai.workspace.radioplayer.*` |

Refresh desktop and icon caches (safe to ignore errors if those tools are missing):

```bash
update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
gtk-update-icon-cache -f -t "$HOME/.local/share/icons/hicolor" 2>/dev/null || true
```

Then search for **Radio Player** in your app menu — it should show with the radio icon. You can also run:

```bash
~/.local/bin/radio-player
```

Ensure `~/.local/bin` is on your `PATH` if you launch from a terminal.

### System-wide install (optional)

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build -j"$(nproc)"
sudo cmake --install build
sudo update-desktop-database /usr/local/share/applications 2>/dev/null || true
sudo gtk-update-icon-cache -f -t /usr/local/share/icons/hicolor 2>/dev/null || true
```

### Uninstall (user install)

```bash
rm -f "$HOME/.local/bin/radio-player"
rm -f "$HOME/.local/share/applications/ai.workspace.radioplayer.desktop"
rm -f "$HOME/.local/share/icons/hicolor/"*/apps/ai.workspace.radioplayer.*
update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
gtk-update-icon-cache -f -t "$HOME/.local/share/icons/hicolor" 2>/dev/null || true
```
---

## Features

- **Home** view driven by the server: recently played, most played, top artists, newly added, highest rated, favorites, and a random “discover” row
- Browse by **artist** and **album**
- Playback controls: previous / play-pause / next, seek bar, volume
- Play queue with **shuffle** and **repeat** (off, all songs, or one song)
- **Favorites** and **1–5 star ratings** saved back to your Navidrome account
- Listening reports to the server (see [scrobbling](#how-listening-is-recorded-on-the-server-scrobbling) above) so play history stays accurate
- Album art cached on disk for about two weeks so covers load quickly next time
- Remembers your last queue and place in the song on quit; press play to resume
- **Equalizer** (10 bands) with genre presets; turn it off for normal/flat sound
- Optional **local network discovery** to find a Navidrome server without typing its address
- **Minimal player** mode: hide the browse panes and shrink to a compact, draggable player window (toggle in the player bar; preference is remembered)
- **Beat stage** on Home while a song is playing: random on-beat visuals (fireworks, neon bars, color snow, starfield, and more) with shifting colors. Analysis runs on a leaky side-chain so **playback stays primary** if visuals fall behind
- **Background library sync**: Home rows, artists, and albums refresh periodically on a worker thread with its own network session (~45s after connect, then about every 3 minutes), so new server content appears without navigating away — and without interrupting playback

---

## Project layout

```text
src/                 Application source code
  audio/             GStreamer playback, EQ, beat-level analysis
  ui/                GTK windows (including Home beat stage)
  queue/             Play queue / shuffle / repeat
  subsonic/          Navidrome / Subsonic API client
  util/              Shared helpers (scrobble rules, safe callbacks)
tests/               Unit tests (queue, config/session, scrobble, beat math, …)
data/icons/          Application icon
data/*.desktop.in    Desktop menu entry template
CMakeLists.txt       Build instructions for CMake
LICENSE              MIT license text
```

---

## Privacy note

Your server username and password are stored in plain text under `~/.config/radio-player/` so the app can reconnect. Do not share that folder, and only use the app on accounts you trust.

---

## Troubleshooting

**App builds but won’t play audio**

```bash
gst-inspect-1.0 playbin >/dev/null && echo "playbin OK"
```

If that fails, install the GStreamer plugin packages listed in the dependency section for your distro.

**Connect fails**

- Confirm the URL includes `http://` or `https://` and the correct port (often **4533**)
- Confirm your Navidrome user is allowed to use API / Subsonic clients
- Try **Discover on LAN**, or test the same URL in another music client

**Clean rebuild**

```bash
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

---

## Contributing

Issues and pull requests are welcome. Please run the unit tests before opening a PR:

```bash
ctest --test-dir build --output-on-failure
```

---

## License

This project is released under the [MIT License](LICENSE).
