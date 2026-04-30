
# ☁️ CloudRip

**CloudRip** ist ein in C geschriebener, selbstgehosteter Cloud-Gaming-Server. Er isoliert Spiele in Docker-Containern und streamt sie via **WebRTC** direkt in den Browser. 

*   **Keine Plugins** erforderlich.
*   **Keine Client-Installation** nötig.
*   **Geringe Latenz** durch WebRTC-Datenkanäle.

---

## ⚙️ Funktionsweise

Jede Spiel-Session läuft in einem isolierten Docker-Container mit einem virtuellen X11-Display (**Xvfb**). 
1.  **Video:** `ffmpeg` greift das X11-Display ab und streamt VP8-Video via WebRTC.
2.  **Input:** Maus- und Tastatureingaben werden vom Browser über den WebRTC-DataChannel gesendet und mittels `libxdo` direkt in den Container injiziert.

### Datenfluss-Diagramm

| Kanal | Wegbeschreibung |
| :--- | :--- |
| **Video** | `Browser` ◀—— `WebRTC` ◀—— `ffmpeg` ◀—— `Xvfb` |
| **Input** | `Browser` ——▶ `DataChannel` ——▶ `libxdo` ——▶ `Xvfb` |

---

## 🛠 Voraussetzungen

Folgende Pakete müssen auf dem Host-System installiert sein:

*   **Build-Tools:** `gcc`, `make`
*   **Virtualisierung:** `docker` (Daemon muss aktiv sein)
*   **X11 & Input:** `Xvfb`, `xdotool`, `libxdo`, `libX11`
*   **Web & Krypto:** `libdatachannel`, `libmicrohttpd`, `libsqlite3`, `libsodium`

---

## 🚀 Installation & Build

### 1. Kompilieren des Servers
Führe diesen Befehl im Projektverzeichnis aus, um die ausführbare Datei zu erstellen:

```bash
mkdir -p build
gcc -g -Wall -Wextra \
    -I./src -I./include \
    ./src/*.c \
    -o build/cloudrip \
    -ldatachannel -lm -lpthread -l:libmicrohttpd.so.12 -ldl -lsqlite3 -lsodium -lxdo -lX11
```

### 2. Game-Container vorbereiten
Bevor eine Session gestartet werden kann, muss das Basis-Image für die Spiele gebaut werden:

```bash
docker build -t cloudrip-container .
```

---

## 📋 Betrieb

Starten Sie den Server immer aus dem **Root-Verzeichnis** des Repos, damit die Pfade zu `/html` und `/data` korrekt gefunden werden:

```bash
./build/cloudrip
```

*   **URL:** `http://localhost:8000`
*   **Datenbank:** Wird automatisch unter `data/database.db` angelegt.

### Standard-Accounts

| Benutzername | Passwort | Rolle |
| :--- | :--- | :--- |
| `admin` | `admin` | Administrator |
| `test` | `test` | Standard-User |

---

## 🎮 Spiele hinzufügen

Neue Spiele können einfach über die Datenbank oder das Admin-Panel hinzugefügt werden.

### Beispiel via SQL:
```sql
INSERT INTO games (gamename, gamedescription, launch_command, is_active)
VALUES ('My Game', 'Ein cooles Spiel', 'command-to-run', 1);
```
> **Hinweis:** Der `launch_command` wird innerhalb des Containers via `/bin/sh -c` ausgeführt. `DISPLAY` ist bereits vorkonfiguriert.

---

## 📂 Architektur-Übersicht

| Datei | Beschreibung |
| :--- | :--- |
| `src/main.c` | Einstiegspunkt, HTTP-Server & Passwort-Logik. |
| `src/multimanager.c` | Verwaltung der WebRTC-Sessions & Streaming-Threads. |
| `src/api_handlers.c` | Logik für Auth, Games-CRUD und Session-Management. |
| `src/docker.c` | Steuerung der Docker-Container und Input-Injection. |

---

## 🤖 KI-Nutzung
Dieses Projekt wurde unter Zuhilfenahme von KI (Claude/Copilot) realisiert:
*   **Frontend:** Alle HTML/CSS-Dateien wurden mit KI-Unterstützung erstellt.
*   **Stabilität:** Die KI half dabei, den ursprünglichen C-Code (Single-User) für den Multi-User-Betrieb abzusichern, um Abstürze bei parallelen Zugriffen zu verhindern.
*   **Readme:** Gemini hat den Plaintext in Makrdown verwandelt.
