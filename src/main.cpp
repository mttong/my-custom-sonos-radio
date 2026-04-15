#include "config.h"
#include "smapi_handler.h"
#include "media_handler.h"
#include "sonos_discovery.h"
#include "control_api.h"
#include "oauth_handler.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <vector>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using json = nlohmann::json;

// ── Discover ngrok public URL via its local API ───────────────────────────────
// ngrok exposes its tunnel list at http://ngrok:4040/api/tunnels (in Docker)
// or http://localhost:4040/api/tunnels (native).
static std::string fetchNgrokUrl(const std::string& host = "ngrok",
                                  int port = 4040,
                                  int max_tries = 20) {
    for (int attempt = 0; attempt < max_tries; ++attempt) {
        try {
            httplib::Client cli(host, port);
            cli.set_connection_timeout(2);
            auto res = cli.Get("/api/tunnels");
            if (res && res->status == 200) {
                auto j = json::parse(res->body);
                for (auto& tunnel : j["tunnels"]) {
                    std::string proto  = tunnel.value("proto", "");
                    std::string pub_url= tunnel.value("public_url", "");
                    if (proto == "https" && !pub_url.empty()) {
                        std::cout << "[ngrok] Public URL: " << pub_url << "\n";
                        return pub_url;
                    }
                }
            }
        } catch (...) {}

        std::cout << "[ngrok] Waiting for tunnel (attempt "
                  << (attempt + 1) << "/" << max_tries << ")...\n";
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    return {};
}

// ── Simple HTML status page ───────────────────────────────────────────────────
static std::string statusPage(const Config& cfg,
                               const std::vector<SonosPlayer>& players) {
    std::string players_html;
    for (auto& p : players) {
        players_html +=
            "<tr><td>" + p.name + "</td><td>" + p.ip + "</td>"
            "<td>" + p.model + "</td><td>" + p.uuid + "</td></tr>";
    }

    std::string auth_status = cfg.access_token.empty()
        ? "<a href='/auth/login' style='color:orange'>Click here to link Sonos account</a>"
        : "<span style='color:green'>Linked</span>";

    return R"(<!DOCTYPE html><html><head>
<title>Maggie Sonos Server</title>
<style>
  body{font-family:sans-serif;max-width:800px;margin:40px auto;padding:0 20px;}
  table{width:100%;border-collapse:collapse;}
  th,td{border:1px solid #ccc;padding:8px;text-align:left;}
  th{background:#f4f4f4;}
  .tag{display:inline-block;padding:2px 8px;border-radius:4px;font-size:.85em;}
  .ok{background:#d4edda;color:#155724;}
  .warn{background:#fff3cd;color:#856404;}
  pre{background:#f8f8f8;padding:12px;border-radius:4px;overflow:auto;}
</style>
</head><body>
<h1>Maggie Local Music - Sonos Server</h1>
<table>
  <tr><td><b>SMAPI Endpoint</b></td><td><code>)" + cfg.public_base_url() + R"(/smapi</code></td></tr>
  <tr><td><b>Media Base URL</b></td><td><code>)" + cfg.public_base_url() + R"(/media/</code></td></tr>
  <tr><td><b>Media Directory</b></td><td><code>)" + cfg.media_dir + R"(</code></td></tr>
  <tr><td><b>Control API Auth</b></td><td>)" + auth_status + R"(</td></tr>
</table>

<h2>Discovered Sonos Players</h2>
<form method='POST' action='/api/discover'>
  <button type='submit'>Scan Network</button>
</form>
<table>
  <tr><th>Name</th><th>IP</th><th>Model</th><th>UUID</th></tr>
  )" + (players_html.empty() ? "<tr><td colspan=4>None found- click Scan Network</td></tr>" : players_html) + R"(
</table>

<h2>Register with Sonos App (SMAPI)</h2>
<p>In the Sonos app: <b>Settings -> Services &amp; Voice -> Add a Service</b><br>
Your service will appear once registered via the developer portal with:<br>
<code>SMAPI Endpoint: )" + cfg.public_base_url() + R"(/smapi</code></p>

<h2>API Reference</h2>
<pre>
GET  /api/households        → list groups
POST /api/play              {"groupId":"..."}
POST /api/pause             {"groupId":"..."}
POST /api/next              {"groupId":"..."}
POST /api/volume            {"groupId":"...", "volume":50}
GET  /api/status/:groupId   → playback state
POST /api/discover          → re-scan for Sonos players
GET  /auth/login            → link Sonos account (Control API)
</pre>
</body></html>)";
}

// ── Main ──────────────────────────────────────────────────────────────────────
int main() {
    // Disable stdout buffering so logs appear immediately in docker logs
    std::cout << std::unitbuf;

    Config cfg = Config::load();

    std::cout << "=== Maggie Sonos Integration Server ===\n";
    std::cout << "[config] media_dir=" << cfg.media_dir
              << " port=" << cfg.server_port << "\n";

    // ── Resolve ngrok URL ─────────────────────────────────────────────────
    if (cfg.ngrok_url.empty()) {
        // Try Docker Compose service name first, then localhost
        cfg.ngrok_url = fetchNgrokUrl("ngrok", 4040);
        if (cfg.ngrok_url.empty()) {
            cfg.ngrok_url = fetchNgrokUrl("localhost", 4040, 3);
        }
        if (cfg.ngrok_url.empty()) {
            std::cerr << "[warn] Could not detect ngrok URL. "
                         "Set NGROK_URL in .env if ngrok is not running.\n";
            cfg.ngrok_url = "http://localhost:" + std::to_string(cfg.server_port);
        }
    }
    std::cout << "[config] public_base_url=" << cfg.public_base_url() << "\n";

    // ── Auto-detect LAN IP for media streaming if not explicitly set ──────
    // The Sonos player streams audio directly over LAN (not via ngrok cloud).
    // We detect the laptop's current LAN IP by routing a UDP socket — works
    // on any WiFi network without sending any packets.
    if (cfg.media_base_url.empty()) {
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock >= 0) {
            sockaddr_in remote{};
            remote.sin_family = AF_INET;
            remote.sin_port   = htons(80);
            inet_pton(AF_INET, "8.8.8.8", &remote.sin_addr);
            if (connect(sock, reinterpret_cast<sockaddr*>(&remote), sizeof(remote)) == 0) {
                sockaddr_in local{};
                socklen_t len = sizeof(local);
                if (getsockname(sock, reinterpret_cast<sockaddr*>(&local), &len) == 0) {
                    char ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &local.sin_addr, ip, sizeof(ip));
                    cfg.media_base_url = "http://" + std::string(ip) + ":"
                                       + std::to_string(cfg.server_port);
                }
            }
            close(sock);
        }
    }
    if (!cfg.media_base_url.empty()) {
        std::cout << "[config] media_base_url=" << cfg.media_base_url
                  << " (player streams audio directly over LAN)\n";
    }

    // ── Set up HTTP server ────────────────────────────────────────────────
    httplib::Server svr;

    // Keep a shared list of discovered players (updated by /api/discover)
    static std::mutex players_mu;
    static std::vector<SonosPlayer> discovered_players;

    // ── SMAPI SOAP endpoint ───────────────────────────────────────────────
    // Sonos calls POST /smapi for all music browsing and URI resolution
    auto smapi = std::make_shared<SMAPIHandler>(cfg);
    svr.Post("/smapi", [smapi](const httplib::Request& req,
                               httplib::Response& res) {
        std::string action = req.get_header_value("SOAPAction");
        // Strip surrounding quotes that some clients add
        if (!action.empty() && action.front() == '"') {
            action = action.substr(1, action.size() - 2);
        }
        std::string response_xml = smapi->handle(req.body, action);
        res.set_header("Content-Type", "text/xml; charset=utf-8");
        res.set_content(response_xml, "text/xml");
    });

    // ── Media streaming ───────────────────────────────────────────────────
    registerMediaRoutes(svr, cfg);

    // ── OAuth + Control API ───────────────────────────────────────────────
    registerOAuthRoutes(svr, cfg);
    registerControlRoutes(svr, cfg);

    // ── SSDP discovery endpoint ───────────────────────────────────────────
    svr.Post("/api/discover", [](const httplib::Request&,
                                  httplib::Response& res) {
        std::cout << "[discovery] Scanning network...\n";
        auto players = discoverSonosPlayers(4000);
        {
            std::lock_guard<std::mutex> lk(players_mu);
            discovered_players = players;
        }
        json result = json::array();
        for (auto& p : players) {
            result.push_back({
                {"name",    p.name},
                {"ip",      p.ip},
                {"model",   p.model},
                {"uuid",    p.uuid},
                {"version", p.sw_version}
            });
        }
        res.set_header("Location", "/");
        res.set_content(result.dump(2), "application/json");
    });

    // ── Status / web UI ───────────────────────────────────────────────────
    svr.Get("/", [&cfg](const httplib::Request&, httplib::Response& res) {
        std::vector<SonosPlayer> players;
        {
            std::lock_guard<std::mutex> lk(players_mu);
            players = discovered_players;
        }
        res.set_content(statusPage(cfg, players), "text/html");
    });

    // ── Health check ──────────────────────────────────────────────────────
    svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"status":"ok"})", "application/json");
    });

    std::cout << "[server] Listening on "
              << cfg.server_host << ":" << cfg.server_port << "\n";
    std::cout << "[server] Web UI: http://localhost:" << cfg.server_port << "\n";
    std::cout << "[server] SMAPI:  " << cfg.public_base_url() << "/smapi\n";

    svr.listen(cfg.server_host.c_str(), cfg.server_port);
    return 0;
}
