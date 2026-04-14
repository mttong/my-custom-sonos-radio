#pragma once
#include <string>
#include <map>
#include <cstdlib>
#include <stdexcept>

struct TokenPair {
    std::string access_token;
    std::string refresh_token;
};

struct Config {
    std::string media_dir;
    std::string server_host;
    int         server_port;

    // Set automatically at runtime by querying the ngrok API
    std::string ngrok_url;

    // Optional override for media streaming URLs returned by getMediaURI.
    // Set to http://YOUR_LAN_IP:8080 so the Sonos player streams directly
    // over LAN instead of going through ngrok (which the player may not reach).
    std::string media_base_url;

    // Sonos Control API OAuth credentials (from developer.sonos.com)
    std::string sonos_client_id;
    std::string sonos_client_secret;

    // Tokens per household — supports multiple authorized Sonos accounts
    std::map<std::string, TokenPair> household_tokens;

    // Most recently authorized token (used for status page / fallback)
    std::string access_token;
    std::string refresh_token;

    static Config load() {
        Config cfg;
        cfg.media_dir          = env_or("MEDIA_DIR",      "/app/media_assets");
        cfg.server_host        = env_or("SERVER_HOST",    "0.0.0.0");
        cfg.server_port        = std::stoi(env_or("SERVER_PORT", "8080"));
        // PUBLIC_URL takes precedence; NGROK_URL kept for local dev backward compat
        cfg.ngrok_url          = env_or("PUBLIC_URL", env_or("NGROK_URL", ""));
        cfg.media_base_url     = env_or("MEDIA_BASE_URL", "");  // defaults to ngrok_url
        cfg.sonos_client_id    = env_or("SONOS_CLIENT_ID",     "");
        cfg.sonos_client_secret= env_or("SONOS_CLIENT_SECRET", "");
        return cfg;
    }

    // The public base URL Sonos players use to reach us (ngrok HTTPS)
    std::string public_base_url() const { return ngrok_url; }

    // Base URL for media streaming — uses LAN IP if set, otherwise ngrok
    std::string media_url_base() const {
        return media_base_url.empty() ? ngrok_url : media_base_url;
    }

    // The redirect URI registered in the developer portal
    std::string redirect_uri() const { return ngrok_url + "/oauth/callback"; }

private:
    static std::string env_or(const char* key, const char* def) {
        const char* v = std::getenv(key);
        return v ? std::string(v) : std::string(def);
    }
};
