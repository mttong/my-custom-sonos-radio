#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "config.h"

namespace httplib { class Server; }

// ── Data types ────────────────────────────────────────────────────────────────
struct SonosGroup {
    std::string id;
    std::string name;
    std::string coordinator_id;
    std::string playback_state; // "PLAYBACK_STATE_PLAYING" etc.
};

struct SonosHousehold {
    std::string id;
    std::vector<SonosGroup> groups;
};

// ── Control API client ────────────────────────────────────────────────────────
// Wraps the Sonos Control API (https://api.ws.sonos.com/control/api/v1).
// All calls require a valid OAuth access token in cfg.access_token.
class ControlAPI {
public:
    explicit ControlAPI(Config& cfg);

    // Fetch all households + their groups
    std::vector<SonosHousehold> getHouseholds();

    // Playback commands — group_id from getHouseholds()
    nlohmann::json play(const std::string& group_id);
    nlohmann::json pause(const std::string& group_id);
    nlohmann::json skipToNextTrack(const std::string& group_id);
    nlohmann::json skipToPreviousTrack(const std::string& group_id);
    nlohmann::json setVolume(const std::string& group_id, int volume);
    nlohmann::json getPlaybackStatus(const std::string& group_id);

private:
    Config& cfg_;
    static constexpr const char* API_HOST = "api.ws.sonos.com";
    static constexpr const char* API_BASE = "/control/api/v1";

    nlohmann::json apiGet(const std::string& path);
    nlohmann::json apiPost(const std::string& path,
                           const nlohmann::json& body = nullptr);
};

// ── Register /api/* routes on the HTTP server ─────────────────────────────────
void registerControlRoutes(httplib::Server& svr, Config& cfg);
