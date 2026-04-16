#pragma once
#include <string>
#include "config.h"

static std::string statusPage(const Config& cfg) {
    // Build linked accounts list
    std::string accounts_html;
    if (cfg.household_tokens.empty()) {
        accounts_html = "<p style='color:#888'>No accounts linked yet.</p>";
    } else {
        for (auto& [hh_id, _] : cfg.household_tokens) {
            accounts_html +=
                "<div style='display:flex;align-items:center;justify-content:space-between;"
                "padding:10px 14px;border:1px solid #ddd;border-radius:6px;margin-bottom:8px;'>"
                "<span style='font-family:monospace;font-size:.9em;color:#333'>" + hh_id + "</span>"
                "<form method='POST' action='/auth/unlink?householdId=" + hh_id + "' style='margin:0'>"
                "<button type='submit' style='background:#fee2e2;color:#991b1b;border:1px solid #fca5a5;"
                "padding:4px 12px;border-radius:4px;cursor:pointer;font-size:.85em'>Unlink</button>"
                "</form></div>";
        }
    }

    return R"(<!DOCTYPE html><html><head>
<title>Maggie's Custom Radio</title>
<style>
  body{font-family:sans-serif;max-width:560px;margin:60px auto;padding:0 24px;color:#111;}
  h1{font-size:1.6em;margin-bottom:4px;}
  .sub{color:#666;margin-bottom:36px;}
  .card{border:1px solid #e5e7eb;border-radius:8px;padding:24px;margin-bottom:24px;}
  .card h2{margin:0 0 8px;font-size:1.1em;}
  .card p{margin:0 0 16px;color:#555;font-size:.95em;}
  .btn{display:inline-block;background:#000;color:#fff;padding:10px 22px;
       border-radius:6px;text-decoration:none;font-size:.95em;}
  .btn:hover{background:#222;}
  details summary{cursor:pointer;color:#888;font-size:.85em;margin-top:8px;}
  pre{background:#f8f8f8;padding:12px;border-radius:4px;overflow:auto;font-size:.82em;}
</style>
</head><body>
<h1>Maggie's Custom Radio</h1>
<p class='sub'>Self-hosted music for Sonos</p>

<div class='card'>
  <h2>Control your Sonos</h2>
  <p>Link your Sonos account to enable play, pause, and volume control via the REST API.</p>
  <a href='/auth/login' class='btn'>Link Sonos Account</a>
</div>

<div class='card'>
  <h2>Linked Accounts</h2>
  )" + accounts_html + R"(
</div>

<details>
  <summary>Server info &amp; API reference</summary>
  <pre>
SMAPI:  )" + cfg.public_base_url() + R"(/smapi
Media:  )" + cfg.media_url_base() + R"(/media/

GET  /api/households        → list groups
POST /api/play              {"groupId":"..."}
POST /api/pause             {"groupId":"..."}
POST /api/next              {"groupId":"..."}
POST /api/volume            {"groupId":"...", "volume":50}
GET  /api/status/:groupId   → playback state
GET  /auth/accounts         → list linked households
POST /auth/unlink           → unlink all
POST /auth/unlink?householdId=X → unlink one
  </pre>
</details>
</body></html>)";
}
