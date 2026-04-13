#pragma once
#include <string>
#include "config.h"

// Registers the /media/* route on the httplib server.
// Streams local MP3 files with full HTTP Range request support so
// Sonos players can seek/pause/resume correctly.

// Forward-declare httplib types to avoid including the heavy header here.
namespace httplib { class Server; }

void registerMediaRoutes(httplib::Server& svr, const Config& cfg);
