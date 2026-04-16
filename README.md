# Maggie's Custom Radio

A self-hosted music service for Sonos built in C++. Streams a curated personal library to any Sonos player via the Sonos Music API (SMAPI). Running 24/7 on a cloud server at `https://maggie-sonos.ngrok-free.app`.

---

## Using the service

### Want access?

The service is in **sandbox mode** — Sonos requires your household ID to be allowlisted before you can add it. Contact me and I'll get you added.

Once added:

1. Open the **Sonos desktop app** (Windows or Mac)
2. Go to **Settings → Services & Voice → Add a Service**
3. Find **Maggie's Custom Radio** and tap **Add**
4. Complete the linking flow

> **Note:** Use the desktop app (Windows or Mac) for the initial setup — authentication on the iOS/Android app is unreliable in sandbox mode. Once you've linked on desktop you can go back to using the mobile app normally for browsing and playback.

### What's in the library

Browse albums, search by artist or track name, and play directly to any Sonos speaker. Tracks are served from the cloud server — no local machine needs to be running.

---

## How it works

```
Browsing:   Sonos app → Sonos cloud → ngrok → VM server
Streaming:  Sonos player → ngrok → VM server → audio
```

All traffic routes through a cloud VM via ngrok. No local machine required once deployed.

---

## Self-hosting

### Prerequisites

- A cloud VM (DigitalOcean, AWS, etc.) or a machine with a public IP
- Docker and Docker Compose
- A free [ngrok](https://ngrok.com) account with a static domain
- A [Sonos developer account](https://developer.sonos.com)

### 1. Clone the repo

```bash
git clone --recurse-submodules git@github.com:mttong/my-custom-sonos-radio.git
cd my-custom-sonos-radio
```

### 2. Add your music

Drop MP3 folders into `media_assets/`. Each subfolder becomes an album. Add a `cover.jpg` inside each folder for album art. Track titles, artist, and album are read from embedded ID3 tags — filenames are used as fallback.

```
media_assets/
  Daft Punk - Alive 2007/
    one_more_time.mp3
    harder_better_faster_stronger.mp3
    cover.jpg
```

To sync your local media_assets to the VM:

```bash
./sync_media.sh
```

### 3. Set up ngrok

1. Sign up at [ngrok.com](https://ngrok.com)
2. Claim a **free static domain** at [dashboard.ngrok.com/domains](https://dashboard.ngrok.com/domains)
3. Copy your auth token from [dashboard.ngrok.com/get-started/your-authtoken](https://dashboard.ngrok.com/get-started/your-authtoken)

### 4. Register a Sonos Music Service (SMAPI)

1. Go to [developer.sonos.com](https://developer.sonos.com) and create a **Music Service Integration**
2. Set the SMAPI endpoint to `https://<your-ngrok-domain>/smapi`
3. Set authentication type to **Anonymous**
4. Add household IDs under **Sandbox Users** for anyone you want to grant access

### 5. Register a Sonos Control Integration (optional)

Enables play/pause/volume via the REST API.

1. Create a **Control Integration** at [developer.sonos.com](https://developer.sonos.com)
2. Set the redirect URI to `https://<your-ngrok-domain>/oauth/callback`
3. Copy the **Client ID** and **Client Secret**

### 6. Configure `.env`

```bash
cp .env.example .env
```

```env
NGROK_AUTHTOKEN=your_ngrok_auth_token
NGROK_DOMAIN=your-subdomain.ngrok-free.app
NGROK_URL=https://your-subdomain.ngrok-free.app

SONOS_CLIENT_ID=your_sonos_client_id
SONOS_CLIENT_SECRET=your_sonos_client_secret
```

### 7. Deploy

```bash
./deploy.sh
```

Builds the Docker image, starts the server and ngrok tunnel, waits for health, and tails the logs.

```bash
./stop.sh   # stop everything
```

---

## REST API

```
GET  /api/households          → list groups and their IDs
POST /api/play                {"groupId":"..."}
POST /api/pause               {"groupId":"..."}
POST /api/next                {"groupId":"..."}
POST /api/volume              {"groupId":"...", "volume":50}
GET  /api/status/:groupId     → current playback state
GET  /auth/login              → start OAuth flow (Control API)
GET  /health                  → health check
```

Example:

```bash
curl https://maggie-sonos.ngrok-free.app/api/households

curl -X POST https://maggie-sonos.ngrok-free.app/api/volume \
  -H "Content-Type: application/json" \
  -d '{"groupId":"YOUR_GROUP_ID","volume":30}'
```

---

## Notes

- OAuth tokens are persisted to `tokens.json` and survive restarts
- Adding new music to `media_assets/` is picked up immediately on the next browse — no restart needed
- Access tokens expire after 24h — re-authorize via the web UI at `/auth/login`
