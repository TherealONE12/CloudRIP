
# ☁️ CloudRip

**CloudRip** is a self-hosted cloud gaming server written in **C**. It runs games inside isolated Docker containers and streams them to any modern browser via **WebRTC**.

*   **No plugins** or client installations required.
*   **High performance** using VP8 video encoding.
*   **Low latency** via direct WebRTC DataChannels for input.

---

## ⚙️ How It Works

Each session is assigned an isolated Docker container equipped with a virtual X11 display (**Xvfb**). 

1.  **Video Stream:** `ffmpeg` captures the X11 display and streams it as VP8 video over WebRTC.
2.  **Input Handling:** Keyboard and mouse events are sent from the browser via the WebRTC DataChannel and injected into the container using `libxdo`.

### Data Flow Diagram



| Path | Components |
| :--- | :--- |
| **Video** | `Browser` ◀—— `WebRTC (VP8)` ◀—— `stream_loop` ◀—— `ffmpeg` ◀—— `Xvfb` |
| **Input** | `Browser` ——▶ `DataChannel` ——▶ `onDataChannelMessage` ——▶ `libxdo` ——▶ `Xvfb` |

---

## 🛠 Requirements

The following system packages are required on the host machine:

*   **Build Tools:** `gcc`, `make`
*   **Virtualization:** `docker` (daemon must be running)
*   **X11 & Input:** `Xvfb`, `xdotool`, `libxdo`, `libX11`
*   **Web & Crypto:** `libdatachannel`, `libmicrohttpd`, `libsqlite3`, `libsodium`

---

## 🚀 Installation & Build

### 1. Compile the Server
Run the following command from the repository root:

```bash
mkdir -p build
gcc -g -Wall -Wextra \
    -I./src -I./include \
    ./src/*.c \
    -o build/cloudrip \
    -ldatachannel -lm -lpthread -l:libmicrohttpd.so.12 -ldl -lsqlite3 -lsodium -lxdo -lX11
```

### 2. Build the Game Image
A base Docker image is required before any session can start:

```bash
docker build -t cloudrip-container .
```

---

## 📋 Running the Server

Execute the binary from the **root directory** so that paths to `html/` and `data/` resolve correctly:

```bash
./build/cloudrip
```

*   **Port:** The server listens on `8000`.
*   **Database:** Automatically created at `data/database.db` on first launch.

### Default Credentials

| Username | Password | Role |
| :--- | :--- | :--- |
| `admin` | `admin` | Administrator |
| `test` | `test` | Standard User |

---

## 🎮 Game Management

### Adding a Game
You can add games via the Admin Panel or by inserting a row into the SQLite database:

```sql
INSERT INTO games (gamename, gamedescription, launch_command, is_active)
VALUES ('My Game', 'Game Description', 'the-launch-command', 1);
```
> **Note:** The `launch_command` runs inside the container via `/bin/sh -c`. The `DISPLAY` environment variable is automatically pre-configured.

---

## 📂 Architecture Overview

| File | Purpose |
| :--- | :--- |
| `src/main.c` | HTTP server (libmicrohttpd), DB initialization, and password hashing. |
| `src/multimanager.c` | WebRTC lifecycle, streaming threads, and session launching. |
| `src/api_handlers.c` | Logic for Auth, Token management, and Game CRUD. |
| `src/docker.c` | Docker daemon communication and `libxdo` input injection. |

---

## 🤖 AI Usage Disclosure
Artificial Intelligence (Claude/Copilot) was utilized in the following areas:
*   **Frontend:** All HTML/CSS files were AI-generated.
*   **Stability:** AI was used to refactor the core C logic from a "proof of concept" to a robust multi-user system, ensuring the server handles concurrent connections without crashing.
*   **Readme:** Translated to markdown format and to englisch.
