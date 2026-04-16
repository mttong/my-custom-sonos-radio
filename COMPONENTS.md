# Key Components

## Tech Stack

| Component | Library / Tool |
|---|---|
| HTTP server | [cpp-httplib](https://github.com/yhirose/cpp-httplib) (header-only, HTTPS via OpenSSL) |
| SOAP XML parsing | [tinyxml2](https://github.com/leethomason/tinyxml2) |
| JSON | [nlohmann/json](https://github.com/nlohmann/json) |
| Build system | CMake 3.20+ with FetchContent |
| Runtime | Docker on Ubuntu 22.04 |
| Tunnel | ngrok static domain |
| Language | C++17 |

Built from scratch — no SMAPI SDK or framework.

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                      Sonos App                          │
└────────────────────────┬────────────────────────────────┘
                         │ SMAPI (SOAP over HTTPS)
                         ▼
                  Sonos Cloud Proxy
                         │
                         ▼
                   ngrok tunnel
                         │
                         ▼
┌─────────────────────────────────────────────────────────┐
│                    Cloud VM (8080)                       │
│                                                         │
│   POST /smapi       →  SMAPIHandler                     │
│   GET  /media/*     →  MediaHandler                     │
│   GET  /auth/*      →  OAuthHandler (Control API)       │
│   GET  /api/*       →  ControlAPI                       │
│   GET  /            →  Status page                      │
└─────────────────────────────────────────────────────────┘
```

SMAPI browse calls are proxied through the Sonos cloud, requiring a public
HTTPS URL. Audio streams directly from the VM to the Sonos player via ngrok.

---

## Source Files

| File | Responsibility |
|---|---|
| `src/main.cpp` | Server setup, route registration, startup |
| `src/smapi_handler.cpp` | All SMAPI SOAP method handlers |
| `src/smapi_handler.h` | TrackInfo / FolderInfo structs, class declaration |
| `src/media_handler.cpp` | Serves MP3/FLAC files with range request support |
| `src/oauth_handler.cpp` | Sonos OAuth flow, token storage, Control API auth |
| `src/control_api.cpp` | REST API — play, pause, next, volume, households |
| `src/config.h` | Config struct, env var loading, URL helpers |
| `src/status_page.h` | Web UI — linked accounts dashboard |
| `src/xml_utils.h` | SOAP envelope helpers, xmlEscape, urlEncode |

---

## SMAPI Methods Implemented

| Method | Description |
|---|---|
| `getMetadata` | Browse root (album list) and folder contents (track list) |
| `getMediaMetadata` | Return track metadata by ID |
| `getMediaURI` | Return streamable audio URL for a track |
| `getLastUpdate` | Return catalog version (mtime-based) for cache invalidation |
| `search` | Case-insensitive substring search across folders and tracks |
| `getMatchingSonosMusicObjects` | Voice control — ranked folder + track results |
| `getExtendedMetadata` | Richer metadata before playback (same as getMediaMetadata) |
| `reportAccountAction` | Playback event reporting (acknowledged, not stored) |
| `getAppLink` | Service onboarding link for Anonymous auth |

---

## Media Library

Files live in `media_assets/` — each subfolder is an album:

```
media_assets/
  Daft Punk - Alive 2007/
    one_more_time.mp3        ← ID3 tags read for title/artist/album
    cover.jpg                ← album art (also: folder.jpg, {foldername}.jpg)
```

**ID3 tag reading** — a lightweight ID3v2 parser is built directly into
`smapi_handler.cpp` (no external library). Reads TIT2, TPE1, TALB frames.
Falls back to filename/folder name if tags are missing.

**Track IDs** follow the pattern `track:{folder}/{filename}`.
**Folder IDs** follow the pattern `folder:{foldername}`.

---

## OAuth & Multi-Household Tokens

The Sonos Control API uses OAuth 2.0. Tokens are stored in `tokens.json`
keyed by household ID, supporting multiple authorized accounts:

```json
{
  "households": {
    "household_abc123": {
      "access_token": "...",
      "refresh_token": "...",
      "name": "Living Room"
    }
  }
}
```

Users authorize via the web UI at `/auth/login`. The OAuth callback
fetches the household ID from the Sonos API and stores the token against it.
Individual households can be unlinked from the dashboard without affecting others.

---

## Known Limitations

- **Sandbox only** — household IDs must be manually allowlisted in the Sonos developer portal
- **Anonymous auth** — SMAPI browsing does not validate auth tokens (fails `test_search_invalid_token` self-test)
- **Access tokens expire after 24h** — re-authorization required via `/auth/login`
- **Voice control** — `getMatchingSonosMusicObjects` is implemented but voice control may require additional portal configuration
- **No search facets** — artists, albums, tracks are not registered as separate search catalogs (single `root` catalog only)
- **Grid view broken on mobile** — Container Display Type=Grid causes "No items available" on iOS after re-adding the service

---

## Open Issues

See [GitHub Issues](https://github.com/mttong/my-custom-sonos-radio/issues) for planned features and known bugs.
