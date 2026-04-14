#include "control_api.h"

#include <httplib.h>
#include <iostream>

using json = nlohmann::json;

// ── ControlAPI implementation ─────────────────────────────────────────────────
ControlAPI::ControlAPI(Config& cfg) : cfg_(cfg) {}

// ── Token helpers ─────────────────────────────────────────────────────────────
std::string ControlAPI::tokenForGroup(const std::string& group_id) const {
    auto it = group_to_household_.find(group_id);
    if (it != group_to_household_.end()) {
        auto tok_it = cfg_.household_tokens.find(it->second);
        if (tok_it != cfg_.household_tokens.end())
            return tok_it->second.access_token;
    }
    // Fall back to first available token
    if (!cfg_.household_tokens.empty())
        return cfg_.household_tokens.begin()->second.access_token;
    return cfg_.access_token;
}

json ControlAPI::apiGetAs(const std::string& path, const std::string& token) const {
    httplib::SSLClient cli(API_HOST, 443);
    cli.set_connection_timeout(10);
    cli.set_bearer_token_auth(token.c_str());
    auto res = cli.Get((std::string(API_BASE) + path).c_str());
    if (!res) return {{"error", "no_response"}, {"path", path}};
    if (res->status != 200) {
        std::cerr << "[control] GET " << path << " → " << res->status << "\n";
        return {{"error", "api_error"}, {"status", res->status}, {"body", res->body}};
    }
    try { return json::parse(res->body); }
    catch (...) { return {{"error", "parse_error"}, {"body", res->body}}; }
}

json ControlAPI::apiPostAs(const std::string& path, const std::string& token,
                            const json& body) const {
    httplib::SSLClient cli(API_HOST, 443);
    cli.set_connection_timeout(10);
    cli.set_bearer_token_auth(token.c_str());
    std::string body_str = body.is_null() ? "{}" : body.dump();
    auto res = cli.Post((std::string(API_BASE) + path).c_str(),
                        body_str, "application/json");
    if (!res) return {{"error", "no_response"}, {"path", path}};
    if (res->status != 200 && res->status != 204) {
        std::cerr << "[control] POST " << path << " → " << res->status << "\n";
        return {{"error", "api_error"}, {"status", res->status}, {"body", res->body}};
    }
    if (res->body.empty()) return {{"ok", true}};
    try { return json::parse(res->body); }
    catch (...) { return {{"ok", true}}; }
}

json ControlAPI::apiGet(const std::string& path) {
    if (cfg_.household_tokens.empty()) {
        return {{"error", "not_authenticated"},
                {"message", "Visit /auth/login to authorise with Sonos"}};
    }
    return apiGetAs(path, cfg_.household_tokens.begin()->second.access_token);
}

json ControlAPI::apiPost(const std::string& path, const json& body) {
    if (cfg_.household_tokens.empty()) {
        return {{"error", "not_authenticated"},
                {"message", "Visit /auth/login to authorise with Sonos"}};
    }
    return apiPostAs(path, cfg_.household_tokens.begin()->second.access_token, body);
}

std::vector<SonosHousehold> ControlAPI::getHouseholds() {
    std::vector<SonosHousehold> result;

    // Iterate over every authorized household token
    for (auto& [hh_id, token_pair] : cfg_.household_tokens) {
        auto hh_json = apiGetAs("/households", token_pair.access_token);
        if (!hh_json.contains("households")) continue;

        for (auto& hh : hh_json["households"]) {
            SonosHousehold household;
            household.id = hh.value("id", "");

            auto groups_json = apiGetAs("/households/" + household.id + "/groups",
                                        token_pair.access_token);
            if (groups_json.contains("groups")) {
                for (auto& g : groups_json["groups"]) {
                    SonosGroup group;
                    group.id             = g.value("id", "");
                    group.name           = g.value("name", "");
                    group.coordinator_id = g.value("coordinatorId", "");
                    group.playback_state = g.value("playbackState", "");
                    // Cache which household owns this group
                    group_to_household_[group.id] = household.id;
                    household.groups.push_back(std::move(group));
                }
            }
            result.push_back(std::move(household));
        }
    }
    return result;
}

json ControlAPI::play(const std::string& group_id) {
    return apiPostAs("/groups/" + group_id + "/playback/play",
                     tokenForGroup(group_id));
}

json ControlAPI::pause(const std::string& group_id) {
    return apiPostAs("/groups/" + group_id + "/playback/pause",
                     tokenForGroup(group_id));
}

json ControlAPI::skipToNextTrack(const std::string& group_id) {
    return apiPostAs("/groups/" + group_id + "/playback/skipToNextTrack",
                     tokenForGroup(group_id));
}

json ControlAPI::skipToPreviousTrack(const std::string& group_id) {
    return apiPostAs("/groups/" + group_id + "/playback/skipToPreviousTrack",
                     tokenForGroup(group_id));
}

json ControlAPI::setVolume(const std::string& group_id, int volume) {
    return apiPostAs("/groups/" + group_id + "/groupVolume",
                     tokenForGroup(group_id), {{"volume", volume}});
}

json ControlAPI::getPlaybackStatus(const std::string& group_id) {
    return apiGetAs("/groups/" + group_id + "/playback",
                    tokenForGroup(group_id));
}

// ── Register /api/* routes ────────────────────────────────────────────────────
void registerControlRoutes(httplib::Server& svr, Config& cfg) {
    auto api = std::make_shared<ControlAPI>(cfg);

    // GET /api/households — list households and groups
    svr.Get("/api/households", [api](const httplib::Request&,
                                     httplib::Response& res) {
        auto households = api->getHouseholds();
        json result = json::array();
        for (auto& hh : households) {
            json groups = json::array();
            for (auto& g : hh.groups) {
                groups.push_back({
                    {"id",             g.id},
                    {"name",           g.name},
                    {"coordinatorId",  g.coordinator_id},
                    {"playbackState",  g.playback_state}
                });
            }
            result.push_back({{"id", hh.id}, {"groups", groups}});
        }
        res.set_content(result.dump(2), "application/json");
    });

    // POST /api/play  body: {"groupId":"..."}
    svr.Post("/api/play", [api](const httplib::Request& req,
                                httplib::Response& res) {
        try {
            auto body     = json::parse(req.body);
            auto group_id = body.at("groupId").get<std::string>();
            res.set_content(api->play(group_id).dump(2), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/pause  body: {"groupId":"..."}
    svr.Post("/api/pause", [api](const httplib::Request& req,
                                 httplib::Response& res) {
        try {
            auto body     = json::parse(req.body);
            auto group_id = body.at("groupId").get<std::string>();
            res.set_content(api->pause(group_id).dump(2), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/next  body: {"groupId":"..."}
    svr.Post("/api/next", [api](const httplib::Request& req,
                                httplib::Response& res) {
        try {
            auto body     = json::parse(req.body);
            auto group_id = body.at("groupId").get<std::string>();
            res.set_content(api->skipToNextTrack(group_id).dump(2), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/volume  body: {"groupId":"...", "volume": 50}
    svr.Post("/api/volume", [api](const httplib::Request& req,
                                  httplib::Response& res) {
        try {
            auto body     = json::parse(req.body);
            auto group_id = body.at("groupId").get<std::string>();
            int  volume   = body.at("volume").get<int>();
            res.set_content(api->setVolume(group_id, volume).dump(2), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // GET /api/status/:groupId
    svr.Get(R"(/api/status/(.+))", [api](const httplib::Request& req,
                                         httplib::Response& res) {
        std::string group_id = req.matches[1].str();
        res.set_content(api->getPlaybackStatus(group_id).dump(2), "application/json");
    });
}
