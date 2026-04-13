#pragma once
#include <string>
#include <vector>

struct SonosPlayer {
    std::string ip;
    std::string name;       // "Bedroom" etc.
    std::string uuid;       // UDN from device description
    std::string model;      // "Sonos Era 100" etc.
    std::string sw_version; // firmware version
};

// Send SSDP M-SEARCH on the local network and fetch device descriptions.
// timeout_ms: how long to wait for UDP replies.
std::vector<SonosPlayer> discoverSonosPlayers(int timeout_ms = 3000);
