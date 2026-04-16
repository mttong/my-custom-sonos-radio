#include "oauth_handler.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <random>
#include <string>

using json = nlohmann::json;

// ── Sonos OAuth endpoints ─────────────────────────────────────────────────────
static constexpr const char* SONOS_AUTH_HOST  = "api.sonos.com";
static constexpr const char* SONOS_AUTH_PATH  =
    "/login/v3/oauth"
    "?client_id={CLIENT_ID}"
    "&response_type=code"
    "&state={STATE}"
    "&scope=playback-control-all"
    "&redirect_uri={REDIRECT_URI}";
static constexpr const char* SONOS_TOKEN_PATH = "/login/v3/oauth/access";

static const std::string TOKEN_FILE = "/app/tokens.json";

// ── Random state string (CSRF protection) ────────────────────────────────────
static std::string generateState() {
    static const char chars[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(chars) - 2);
    std::string s(32, ' ');
    for (auto& c : s) c = chars[dis(gen)];
    return s;
}

// ── URL-encode a value (for OAuth params) ────────────────────────────────────
static std::string oauthEncode(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

// ── Persist tokens to disk ────────────────────────────────────────────────────
static void saveTokens(const Config& cfg) {
    try {
        json j;
        for (auto& [hh_id, pair] : cfg.household_tokens) {
            j["households"][hh_id] = {
                {"access_token",  pair.access_token},
                {"refresh_token", pair.refresh_token},
                {"name",          pair.name}
            };
        }
        std::ofstream f(TOKEN_FILE);
        f << j.dump(2);
    } catch (...) {}
}

static void loadTokens(Config& cfg) {
    try {
        std::ifstream f(TOKEN_FILE);
        if (!f) return;
        json j;
        f >> j;

        if (j.contains("households")) {
            // Current format: keyed by household ID
            for (auto& [hh_id, tok] : j["households"].items()) {
                TokenPair pair;
                pair.access_token  = tok.value("access_token",  "");
                pair.refresh_token = tok.value("refresh_token", "");
                pair.name          = tok.value("name",          "");
                if (!pair.access_token.empty())
                    cfg.household_tokens[hh_id] = pair;
            }
        } else if (j.contains("access_token")) {
            // Legacy format: single token pair
            TokenPair pair;
            pair.access_token  = j.value("access_token",  "");
            pair.refresh_token = j.value("refresh_token", "");
            if (!pair.access_token.empty())
                cfg.household_tokens["legacy"] = pair;
        }

        if (!cfg.household_tokens.empty()) {
            cfg.access_token  = cfg.household_tokens.begin()->second.access_token;
            cfg.refresh_token = cfg.household_tokens.begin()->second.refresh_token;
            std::cout << "[oauth] Loaded tokens for "
                      << cfg.household_tokens.size() << " household(s)\n";
        }
    } catch (...) {}
}

// ── Token exchange (code → access_token) ─────────────────────────────────────
static bool exchangeCode(Config& cfg, const std::string& code) {
    httplib::SSLClient cli(SONOS_AUTH_HOST, 443);
    cli.set_connection_timeout(10);

    std::string body =
        "grant_type=authorization_code"
        "&code=" + oauthEncode(code) +
        "&redirect_uri=" + oauthEncode(cfg.redirect_uri());

    // Basic auth: client_id:client_secret (base64-encoded)
    // httplib handles Basic auth via set_basic_auth
    cli.set_basic_auth(cfg.sonos_client_id.c_str(),
                       cfg.sonos_client_secret.c_str());

    auto res = cli.Post(SONOS_TOKEN_PATH, body,
                        "application/x-www-form-urlencoded");
    if (!res) {
        std::cerr << "[oauth] Token exchange: no response from server\n";
        return false;
    }
    if (res->status != 200) {
        std::cerr << "[oauth] Token exchange failed: " << res->status
                  << " " << res->body << "\n";
        return false;
    }

    try {
        auto j = json::parse(res->body);
        std::string access  = j.at("access_token");
        std::string refresh = j.value("refresh_token", "");

        // Fetch the household IDs this token has access to, so we can key by them
        std::vector<std::string> hh_ids;
        {
            httplib::SSLClient hh_cli("api.ws.sonos.com", 443);
            hh_cli.set_connection_timeout(10);
            hh_cli.set_bearer_token_auth(access.c_str());
            auto hh_res = hh_cli.Get("/control/api/v1/households");
            if (hh_res && hh_res->status == 200) {
                auto hh_j = json::parse(hh_res->body);
                for (auto& hh : hh_j.value("households", json::array()))
                    hh_ids.push_back(hh.value("id", ""));
            }
        }
        if (hh_ids.empty()) {
            // Fallback key if household fetch failed
            hh_ids.push_back("household_" + std::to_string(cfg.household_tokens.size()));
        }

        for (auto& hh_id : hh_ids)
            cfg.household_tokens[hh_id] = {access, refresh};

        cfg.access_token  = access;
        cfg.refresh_token = refresh;
        saveTokens(cfg);
        std::cout << "[oauth] Token exchange successful — "
                  << hh_ids.size() << " household(s) authorised\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[oauth] JSON parse error: " << e.what() << "\n";
        return false;
    }
}

// ── Register routes ───────────────────────────────────────────────────────────
void registerOAuthRoutes(httplib::Server& svr, Config& cfg) {
    // Try to restore previously saved tokens on startup
    loadTokens(cfg);

    // State stored in memory (single-user server)
    static std::string pending_state;

    // GET /auth/login — redirects browser to Sonos OAuth page
    svr.Get("/auth/login", [&cfg](const httplib::Request&,
                                  httplib::Response& res) {
        if (cfg.sonos_client_id.empty()) {
            res.set_content(
                "<h2>SONOS_CLIENT_ID not set.</h2>"
                "<p>Add your Client ID to the .env file and restart.</p>",
                "text/html");
            res.status = 503;
            return;
        }

        pending_state = generateState();

        std::string url = std::string("https://") + SONOS_AUTH_HOST +
                          "/login/v3/oauth"
                          "?client_id="      + oauthEncode(cfg.sonos_client_id) +
                          "&response_type=code"
                          "&state="          + oauthEncode(pending_state) +
                          "&scope=playback-control-all"
                          "&redirect_uri="   + oauthEncode(cfg.redirect_uri());

        res.set_redirect(url.c_str());
    });

    // GET /oauth/callback — Sonos redirects here with ?code=XXX&state=YYY
    svr.Get("/oauth/callback", [&cfg](const httplib::Request& req,
                                      httplib::Response& res) {
        std::string state = req.get_param_value("state");
        std::string code  = req.get_param_value("code");
        std::string error = req.get_param_value("error");

        if (!error.empty()) {
            res.set_content("<h2>OAuth error: " + error + "</h2>", "text/html");
            res.status = 400;
            return;
        }

        if (state != pending_state) {
            res.set_content("<h2>State mismatch — possible CSRF</h2>", "text/html");
            res.status = 400;
            return;
        }

        if (exchangeCode(cfg, code)) {
            // Redirect to naming page for the most recently added household
            std::string hh_id = cfg.household_tokens.rbegin()->first;
            res.set_redirect(("/auth/name?householdId=" + hh_id).c_str());
        } else {
            res.set_content("<h2>Token exchange failed — check server logs</h2>",
                            "text/html");
            res.status = 500;
        }
    });

    // GET /auth/status — quick check
    svr.Get("/auth/status", [&cfg](const httplib::Request&,
                                   httplib::Response& res) {
        json j = {
            {"authenticated", !cfg.household_tokens.empty()},
            {"households",    (int)cfg.household_tokens.size()},
            {"client_id_set", !cfg.sonos_client_id.empty()}
        };
        res.set_content(j.dump(2), "application/json");
    });

    // GET /auth/name?householdId=XXX — show naming form after OAuth
    svr.Get("/auth/name", [](const httplib::Request& req,
                              httplib::Response& res) {
        std::string hh_id = req.get_param_value("householdId");
        res.set_content(
            R"(<!DOCTYPE html><html><head>
<title>Name your household</title>
<style>
  body{font-family:sans-serif;max-width:480px;margin:80px auto;padding:0 24px;color:#111;}
  h1{font-size:1.4em;margin-bottom:8px;}
  p{color:#555;margin-bottom:24px;}
  input{width:100%;padding:10px 12px;font-size:1em;border:1px solid #ddd;
        border-radius:6px;box-sizing:border-box;margin-bottom:16px;}
  button{background:#000;color:#fff;padding:10px 22px;border:none;
         border-radius:6px;font-size:.95em;cursor:pointer;}
  button:hover{background:#222;}
</style></head><body>
<h1>Account linked!</h1>
<p>Give this household a name so you can identify it on the dashboard.</p>
<form method='POST' action='/auth/name'>
  <input type='hidden' name='householdId' value=')" + hh_id + R"('>
  <input type='text' name='name' placeholder='e.g. Living Room' autofocus>
  <button type='submit'>Save</button>
</form>
</body></html>)", "text/html");
    });

    // POST /auth/name — save the household name
    svr.Post("/auth/name", [&cfg](const httplib::Request& req,
                                   httplib::Response& res) {
        std::string hh_id = req.get_param_value("householdId");
        std::string name  = req.get_param_value("name");
        auto it = cfg.household_tokens.find(hh_id);
        if (it != cfg.household_tokens.end()) {
            it->second.name = name.empty() ? hh_id : name;
            saveTokens(cfg);
        }
        res.set_redirect("/");
    });

    // GET /auth/accounts — list all linked households
    svr.Get("/auth/accounts", [&cfg](const httplib::Request&,
                                     httplib::Response& res) {
        json accounts = json::array();
        for (auto& [hh_id, _] : cfg.household_tokens)
            accounts.push_back({{"householdId", hh_id}});
        res.set_content(accounts.dump(2), "application/json");
    });

    // POST /auth/unlink?householdId=XXX — remove one household's tokens
    // POST /auth/unlink — remove all tokens
    svr.Post("/auth/unlink", [&cfg](const httplib::Request& req,
                                    httplib::Response& res) {
        std::string hh_id = req.get_param_value("householdId");
        if (hh_id.empty()) {
            cfg.household_tokens.clear();
            cfg.access_token.clear();
            cfg.refresh_token.clear();
            std::cout << "[oauth] All accounts unlinked\n";
        } else {
            cfg.household_tokens.erase(hh_id);
            cfg.access_token = cfg.household_tokens.empty()
                ? "" : cfg.household_tokens.begin()->second.access_token;
            std::cout << "[oauth] Unlinked household: " << hh_id << "\n";
        }
        saveTokens(cfg);
        res.set_content(
            "<h2>Unlinked.</h2><p><a href='/'>Back to status page</a></p>"
            "<p><a href='/auth/login'>Link another account</a></p>",
            "text/html");
    });
}
