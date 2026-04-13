#pragma once
#include <string>
#include "config.h"

namespace httplib { class Server; }

// Registers OAuth 2.0 routes for the Sonos Control API.
//
// Flow:
//   1. User opens browser → http://localhost:8080/auth/login
//   2. Server redirects to Sonos login page
//   3. User authenticates with their Sonos account
//   4. Sonos redirects to /oauth/callback?code=CODE
//   5. Server exchanges code for access + refresh tokens
//   6. Tokens stored in cfg (in-memory + persisted to tokens.json)
//
// After this, control_api.cpp uses cfg.access_token for API calls.
void registerOAuthRoutes(httplib::Server& svr, Config& cfg);
