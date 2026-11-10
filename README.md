# Radio Player

Lightweight **GTK 4 / libadwaita / C++** music player for [Navidrome](https://www.navidrome.org/) (Subsonic API).

Browse Home / Artists / Albums, play queues with shuffle & repeat, rate and favorite tracks, scrobble plays back to the server, and tune audio with a 10-band EQ.

**License:** [MIT](LICENSE)

---

## Requirements

- Linux desktop (Wayland or X11)
- CMake **3.20+**
- C++20 compiler (`g++` or `clang++`)
- GTK 4, libadwaita, GStreamer, libsoup 3, nlohmann-json
- A reachable Navidrome (or other Subsonic-compatible) server

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

Clone your fork/remote (replace the URL with yours):

```bash
git clone https://github.com/YOUR_USER/radio-player.git
cd radio-player
```

Or, if you already have this directory:

```bash
cd /path/to/radio-player
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

Binaries:

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

1. On launch, the **Connect to Navidrome** dialog appears (or click **Server** in the header).
2. Enter:
   - **Server URL** — e.g. `http://192.168.1.10:4533`
   - **Username**
   - **Password**
3. Or click **Discover on LAN** to find servers:
   - UDP Jellyfin discovery on port **7359**
   - TCP probe of port **4533** on your subnet
4. Click **Connect**.

Credentials are stored locally at:

```text
~/.config/radio-player/config.json
```

Playback session (queue / position) is stored at:

```text
~/.config/radio-player/session.json
```

Cover art cache:

```text
~/.cache/radio-player/covers/
```

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

## 7. Optional: install for your user

```bash
cmake --install build --prefix "$HOME/.local"
```

Refresh desktop/icon caches (safe to ignore errors if tools are missing):

```bash
update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
gtk-update-icon-cache -f -t "$HOME/.local/share/icons/hicolor" 2>/dev/null || true
```

Then launch from your app menu as **Radio Player**, or:

```bash
~/.local/bin/radio-player
```

---

## Features

- **Home** feed from the server: recently played, most played, top artists, recently added, highest rated, favorites, discover
- Browse **Artists** and **Albums**
- Transport: previous / play-pause / next, seek, volume
- Queue with **shuffle** and **repeat** (off / all / one)
- Favorites and **1–5 star ratings** synced to the server (`star` / `unstar` / `setRating`)
- **Scrobbling** so Navidrome play counts stay accurate (`scrobble` with `submission=true`)
- Cover art with memory + disk cache (14-day TTL)
- Resume last queue and position on launch (press play to continue)
- 10-band **EQ** with genre presets; can be turned off for flat playback
- LAN auto-discovery for Navidrome

---

## Project layout

```text
src/                 Application sources (UI, audio, Subsonic client, …)
tests/               Unit tests
data/icons/          App icon (hicolor theme)
data/*.desktop.in    Desktop entry template
CMakeLists.txt       Build definition
LICENSE              MIT license
```

---

## Privacy note

Server username and password are saved in plain text under `~/.config/radio-player/` so the app can reconnect. Do not commit that directory, and treat the machine account as trusted.

---

## Troubleshooting

**App builds but won’t play audio**

```bash
# Confirm GStreamer can see plugins
gst-inspect-1.0 playbin >/dev/null && echo "playbin OK"
```

Install the `gstreamer1.0-plugins-*` / `gst-plugins-*` packages listed above if needed.

**Connect fails**

- Check URL scheme (`http://` or `https://`) and port (Navidrome default **4533**)
- Confirm the server allows Subsonic clients for your user
- Try **Discover on LAN**, or ping from another Subsonic client

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
