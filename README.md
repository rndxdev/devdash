# devdash

A lightweight, all-in-one developer dashboard for Linux. Built with GTK3 and C.

## Panels

- **Git** — track branches, changes, and status across multiple projects
- **Processes** — monitor running processes sorted by CPU usage
- **Clipboard** — clipboard history manager
- **Ports** — view listening ports and associated processes
- **Scratchpad** — quick notes with auto-save
- **Logs** — tail log files in real time
- **Env Vars** — browse and search environment variables
- **SSH** — manage SSH connections from `~/.ssh/config`
- **Shell** — embedded terminal
- **Battery** — battery status, health, and charge monitoring
- **System** — CPU, memory, and disk usage

## Prerequisites

Linux only. Install the following packages:

**Ubuntu/Debian:**

```
sudo apt install build-essential libgtk-3-dev libnotify-dev libvte-2.91-dev pkg-config
```

**Fedora:**

```
sudo dnf install gcc make gtk3-devel libnotify-devel vte291-devel pkg-config
```

**Arch:**

```
sudo pacman -S base-devel gtk3 libnotify vte3 pkgconf
```

## Build

```
make
```

## Install

```
sudo make install
```

Installs to `/usr/local/bin/devdash`.

## Run

```
devdash
```

## Keyboard Shortcuts

`Ctrl+1` through `Ctrl+0` to switch between panels.

## Configuration

Config files are stored in `~/.config/devdash/` and created automatically on first run:

- `gitdash.conf` — project paths to monitor (one per line)
- `logwatch.conf` — log file paths to watch (one per line)
- `scratchpad.txt` — scratchpad notes

## License

MIT
