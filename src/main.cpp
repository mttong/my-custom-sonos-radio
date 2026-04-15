#include "config.h"
#include "smapi_handler.h"
#include "media_handler.h"
#include "control_api.h"
#include "oauth_handler.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <iostream>
#include <mutex>
#include <vector>

using json = nlohmann::json;

// ── Simple HTML status page ───────────────────────────────────────────────────
static std::string statusPage(const Config& cfg) {
    std::string auth_status = cfg.access_token.empty()
        ? "<a href='/auth/login' style='color:orange'>Click here to link Sonos account</a>"
        : "<span style='color:green'>Linked (" + std::to_string(cfg.household_tokens.size()) + " household(s))</span>"
          " | <a href='/auth/accounts'>View accounts</a>";

    return R"(<!DOCTYPE html><html><head>
<title>Maggie Sonos Server</title>
<style>
  body{font-family:sans-serif;max-width:800px;margin:40px auto;padding:0 20px;}
  table{width:100%;border-collapse:collapse;}
  th,td{border:1px solid #ccc;padding:8px;text-align:left;}
  th{background:#f4f4f4;}
  pre{background:#f8f8f8;padding:12px;border-radius:4px;overflow:auto;}
</style>
</head><body>
<h1>Maggie's Custom Sonos Radio</h1>
<table>
  <tr><td><b>SMAPI Endpoint</b></td><td><code>)" + cfg.public_base_url() + R"(/smapi</code></td></tr>
  <tr><td><b>Media Base URL</b></td><td><code>)" + cfg.media_url_base() + R"(/media/</code></td></tr>
  <tr><td><b>Media Directory</b></td><td><code>)" + cfg.media_dir + R"(</code></td></tr>
  <tr><td><b>Control API Auth</b></td><td>)" + auth_status + R"(</td></tr>
</table>

<h2>Register with Sonos App (SMAPI)</h2>
<p>In the Sonos app: <b>Settings &rarr; Services &amp; Voice &rarr; Add a Service</b><br>
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
GET  /auth/login            → link Sonos account (Control API)
GET  /auth/accounts         → list linked households
POST /auth/unlink           → unlink all accounts
POST /auth/unlink?householdId=X → unlink one household
</pre>
</body></html>)";
}

// ── Main ──────────────────────────────────────────────────────────────────────
int main() {
    // Disable stdout buffering so logs appear immediately in docker logs
    std::cout << std::unitbuf;

    Config cfg = Config::load();

    std::cout << "=== Maggie's Custom Sonos Radio ===\n";
    std::cout << "[config] media_dir=" << cfg.media_dir
              << " port=" << cfg.server_port << "\n";
    std::cout << "[config] public_base_url=" << cfg.public_base_url() << "\n";
    std::cout << "[config] media_base_url=" << cfg.media_url_base() << "\n";

    // ── Set up HTTP server ────────────────────────────────────────────────
    httplib::Server svr;

    // ── SMAPI SOAP endpoint ───────────────────────────────────────────────
    auto smapi = std::make_shared<SMAPIHandler>(cfg);
    svr.Post("/smapi", [smapi](const httplib::Request& req,
                               httplib::Response& res) {
        std::string action = req.get_header_value("SOAPAction");
        if (!action.empty() && action.front() == '"')
            action = action.substr(1, action.size() - 2);
        std::string response_xml = smapi->handle(req.body, action);
        res.set_header("Content-Type", "text/xml; charset=utf-8");
        res.set_content(response_xml, "text/xml");
    });

    // ── Media streaming ───────────────────────────────────────────────────
    registerMediaRoutes(svr, cfg);

    // ── OAuth + Control API ───────────────────────────────────────────────
    registerOAuthRoutes(svr, cfg);
    registerControlRoutes(svr, cfg);

    // ── Status / web UI ───────────────────────────────────────────────────
    svr.Get("/", [&cfg](const httplib::Request&, httplib::Response& res) {
        res.set_content(statusPage(cfg), "text/html");
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
