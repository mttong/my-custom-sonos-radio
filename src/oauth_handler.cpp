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
static void saveTokens(const std::string& access, const std::string& refresh) {
    try {
        std::ofstream f(TOKEN_FILE);
        json j = {{"access_token", access}, {"refresh_token", refresh}};
        f << j.dump(2);
    } catch (...) {}
}

static void loadTokens(Config& cfg) {
    try {
        std::ifstream f(TOKEN_FILE);
        if (!f) return;
        json j;
        f >> j;
        cfg.access_token  = j.value("access_token",  "");
        cfg.refresh_token = j.value("refresh_token", "");
        if (!cfg.access_token.empty())
            std::cout << "[oauth] Loaded saved tokens from " << TOKEN_FILE << "\n";
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
        cfg.access_token  = j.at("access_token");
        cfg.refresh_token = j.value("refresh_token", "");
        saveTokens(cfg.access_token, cfg.refresh_token);
        std::cout << "[oauth] Token exchange successful\n";
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
            res.set_content(
                "<h2>Authorised!</h2>"
                "<p>Your Sonos account is linked. You can close this tab.</p>"
                "<p><a href='/'>Back to status page</a></p>",
                "text/html");
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
            {"authenticated", !cfg.access_token.empty()},
            {"client_id_set", !cfg.sonos_client_id.empty()}
        };
        res.set_content(j.dump(2), "application/json");
    });
}
