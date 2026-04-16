#include "config.h"
#include "smapi_handler.h"
#include "media_handler.h"
#include "control_api.h"
#include "oauth_handler.h"
#include "status_page.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <iostream>
#include <mutex>
#include <vector>

using json = nlohmann::json;

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
