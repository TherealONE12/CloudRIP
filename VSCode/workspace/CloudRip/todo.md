# CloudRip Beta TODO

- [X] **Session-Architektur einführen (`src/main.c`)**  
  Globalen Single-Client-State entfernen und `ClientSession`-Struct bauen (`sessionId`, `username`, `game`, `display`, `pc`, `track`, `offer/candidates`, `stream-thread`, `pids`).

- [X] **Login-Daten persistieren**  
  `data/`-Ordner + `data/users.db` (einfacher Datei-Store) anlegen.

- [~] **Auth-Token-System hinzufügen**  
  Token-Pool im RAM (`g_tokens`), Token erzeugen, validieren, User aus Token auflösen.

- [X] **Game-Auswahl modellieren**  
  Feste Game-Liste (`id`, `name`, `command`) definieren und `GET /games` Endpoint liefern.

- [~] **HTTP-APIs für Auth bauen**  
  `POST /register` und `POST /login` mit JSON-Parsing, Validierung, Fehlercodes.

- [~] **Session-Start API bauen**  
  `POST /session` implementieren: Token prüfen, Game prüfen, freie Session anlegen, PeerConnection + Track erstellen, Offer erzeugen, `sessionId+sdp+type` zurückgeben.

- [~] **Signaling-Endpunkte pro Session aufbauen**  
  `POST /answer` und `POST /candidate` mit `token + sessionId`; richtigen Session-PC finden und Daten setzen.

- [~] **Runtime pro Session starten/stoppen**  
  Für jede Session eigenes `Xvfb :display` + Game-Prozess starten, bei Disconnect sauber beenden.

- [~] **Streaming-Thread pro Session bauen**  
  `ffmpeg x11grab` je Display starten, IVF lesen, VP8/RTP über `rtcSendMessage` senden.

- [-] **Session-Storage + Locks bauen**  
  `g_sessions[MAX_SESSIONS]` plus `pthread_mutex` für Thread-Sicherheit bei Zugriff aus HTTP-Handlern + RTC-Callbacks.

- [AI] **Frontend umbauen (`html/index.html`)**  
  Register/Login UI, Game-Select, Session-Start, WebRTC connect, alle Input-Requests mit `token + sessionId` senden.

- [ ] **Selbsttests einführen**  
  Selbsttests (sizeof verifizieren, docker installed, ...)

- [ ] **Smoke-Tests durchführen**  
  Build, Serverstart, 2 Browser gleichzeitig, unterschiedliche Games, Login/Fehlerfälle, Maus+Text+Klick+Wheel verifizieren.

- [ ] **README aktualisieren**  
  Build-Befehl, Run-Befehl, Login-Flow, Session-Flow und unterstützte Games dokumentieren.
