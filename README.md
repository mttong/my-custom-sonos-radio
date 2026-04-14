# My Custom Sonos Radio

A self-hosted music service for Sonos built with C++. Serves your local MP3 library to any Sonos player on your network via the Sonos Music API (SMAPI), with voice control support ("Hey Sonos, play boygenius") and a Control API for play/pause/volume commands.

## How it works

```
Browsing & voice:  Sonos app → Sonos cloud → ngrok → your server
Audio streaming:   Sonos player → your laptop LAN IP directly
```

SMAPI browsing calls are proxied through the Sonos cloud, so they need a public HTTPS URL (ngrok). Audio streams directly from your laptop to the Sonos player over WiFi, bypassing ngrok entirely. The server auto-detects your LAN IP at startup so this works on any network.

## Prerequisites

- Docker and Docker Compose
- A free [ngrok](https://ngrok.com) account
- A [Sonos developer account](https://developer.sonos.com)
- A Sonos player (S2 firmware)

---

## Setup

### 1. Clone the repo

```bash
git clone git@github.com:mttong/my-custom-sonos-radio.git
cd my-custom-sonos-radio
```

### 2. Add your music

Drop MP3 folders into `media_assets/`. Each folder becomes an album — the folder name is used as the artist and album name. Add a JPEG named after the folder for album art.

```
media_assets/
  boygenius/
    me_and_my_dog.mp3
    bite_the_hand.mp3
    stay_down.mp3
    boygenius.jpg       ← album art (filename must match folder name)
```

### 3. Set up ngrok

1. Sign up at [ngrok.com](https://ngrok.com)
2. Claim a **free static domain** at [dashboard.ngrok.com/domains](https://dashboard.ngrok.com/domains) — this never changes across restarts, which keeps your Sonos service registration valid permanently
3. Copy your auth token from [dashboard.ngrok.com/get-started/your-authtoken](https://dashboard.ngrok.com/get-started/your-authtoken)

### 4. Register a Sonos Music Service (SMAPI)

1. Go to [developer.sonos.com](https://developer.sonos.com) and create a **Music Service Integration**
2. Set the SMAPI endpoint to `https://<your-ngrok-domain>/smapi`
3. Set authentication type to **Anonymous**
4. Note the **Service ID** — you'll need it to add the service in the Sonos app

### 5. Register a Sonos Control Integration (optional)

Needed for play/pause/volume API commands. Skip if you only want browsing and playback.

1. At [developer.sonos.com](https://developer.sonos.com), create a **Control Integration**
2. Set the redirect URI to `https://<your-ngrok-domain>/oauth/callback`
3. Copy the **Client ID** and **Client Secret**

### 6. Configure `.env`

```bash
cp .env.example .env
```

Open `.env` and fill in:

```env
NGROK_AUTHTOKEN=your_ngrok_auth_token
NGROK_DOMAIN=your-subdomain.ngrok-free.app
NGROK_URL=https://your-subdomain.ngrok-free.app

SONOS_CLIENT_ID=your_sonos_client_id       # only needed for Control API
SONOS_CLIENT_SECRET=your_sonos_client_secret
```

### 7. Start the server

```bash
./demo.sh
```

This builds the Docker image, starts the server and ngrok tunnel, waits for health, and tails the logs. The web UI will open at `http://localhost:8080`.

To stop:

```bash
./stop.sh
```

### 8. Add the service to your Sonos app

> **Note:** Use the **Windows or Mac desktop Sonos app** for initial setup — the iOS app has a known issue with sandbox service authorization.

1. Open the Sonos app → **Settings → Services & Voice → Add a Service**
2. Find your service by name and tap **Add**
3. Complete the linking flow

Your music library will now appear in the Sonos app. Browse by folder, tap a track to play.

### 9. Authorize the Control API (optional)

Visit `http://localhost:8080/auth/login` and complete the OAuth flow to enable play/pause/volume commands.

---

## API Reference

```
GET  /api/households          → list groups and their IDs
POST /api/play                {"groupId":"..."}
POST /api/pause               {"groupId":"..."}
POST /api/next                {"groupId":"..."}
POST /api/volume              {"groupId":"...", "volume":50}
GET  /api/status/:groupId     → current playback state
POST /api/discover            → re-scan network for Sonos players
GET  /auth/login              → start OAuth flow (Control API)
GET  /health                  → health check
```

Example — get groups then set volume:

```bash
curl http://localhost:8080/api/households

curl -X POST http://localhost:8080/api/volume \
  -H "Content-Type: application/json" \
  -d '{"groupId":"YOUR_GROUP_ID","volume":30}'
```

---

## Voice control

With Sonos Voice Control enabled on your player, you can say:

> "Hey Sonos, play boygenius"

The server handles `getMatchingSonosMusicObjects` and matches against your folder and track names.

---

## Notes

- The server auto-detects your LAN IP at startup — audio streaming works on any WiFi network without configuration
- OAuth tokens are persisted to `tokens.json` and survive restarts
- Adding new music to `media_assets/` is picked up immediately on the next browse — no restart needed
