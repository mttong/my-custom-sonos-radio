#include "sonos_discovery.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>

// httplib is used to fetch the device description XML over HTTP
#include <httplib.h>
#include <tinyxml2.h>

// ── SSDP constants ────────────────────────────────────────────────────────────
static constexpr const char* SSDP_ADDR    = "239.255.255.250";
static constexpr int         SSDP_PORT    = 1900;
static constexpr const char* SEARCH_MSG  =
    "M-SEARCH * HTTP/1.1\r\n"
    "HOST: 239.255.255.250:1900\r\n"
    "MAN: \"ssdp:discover\"\r\n"
    "MX: 3\r\n"
    "ST: urn:schemas-upnp-org:device:ZonePlayer:1\r\n"
    "\r\n";

// ── Parse "LOCATION: http://IP:PORT/path" from SSDP reply ────────────────────
static std::string extractLocation(const std::string& resp) {
    std::istringstream ss(resp);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.rfind("LOCATION:", 0) == 0 ||
            line.rfind("Location:", 0) == 0 ||
            line.rfind("location:", 0) == 0) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                std::string url = line.substr(pos + 1);
                // trim whitespace
                url.erase(0, url.find_first_not_of(" \t\r\n"));
                url.erase(url.find_last_not_of(" \t\r\n") + 1);
                return url;
            }
        }
    }
    return {};
}

// ── Parse "http://IP:PORT/path" into (host, port, path) ──────────────────────
static bool parseUrl(const std::string& url,
                     std::string& host, int& port, std::string& path) {
    // strip scheme
    std::string rest = url;
    if (rest.rfind("http://", 0) == 0)  rest = rest.substr(7);
    else if (rest.rfind("https://", 0) == 0) rest = rest.substr(8);
    else return false;

    size_t slash = rest.find('/');
    std::string hostport = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    path = (slash == std::string::npos) ? "/" : rest.substr(slash);

    size_t colon = hostport.find(':');
    if (colon != std::string::npos) {
        host = hostport.substr(0, colon);
        port = std::stoi(hostport.substr(colon + 1));
    } else {
        host = hostport;
        port = 80;
    }
    return !host.empty();
}

// ── Fetch device description XML and populate SonosPlayer ────────────────────
static bool fetchDeviceDesc(const std::string& location, SonosPlayer& player) {
    std::string host, path;
    int port = 80;
    if (!parseUrl(location, host, port, path)) return false;

    player.ip = host;

    httplib::Client cli(host, port);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);

    auto res = cli.Get(path.c_str());
    if (!res || res->status != 200) return false;

    tinyxml2::XMLDocument doc;
    if (doc.Parse(res->body.c_str()) != tinyxml2::XML_SUCCESS) return false;

    auto* root = doc.FirstChildElement("root");
    if (!root) return false;
    auto* device = root->FirstChildElement("device");
    if (!device) return false;

    auto txt = [&](const char* tag) -> std::string {
        auto* el = device->FirstChildElement(tag);
        return (el && el->GetText()) ? el->GetText() : "";
    };

    player.model      = txt("modelName");
    player.sw_version = txt("softwareVersion");

    // UDN looks like "uuid:RINCON_..."
    std::string udn = txt("UDN");
    if (udn.rfind("uuid:", 0) == 0) udn = udn.substr(5);
    player.uuid = udn;

    // Friendly name
    player.name = txt("friendlyName");

    return true;
}

// ── Main discovery function ───────────────────────────────────────────────────
std::vector<SonosPlayer> discoverSonosPlayers(int timeout_ms) {
    std::vector<SonosPlayer> result;
    std::set<std::string> seen_locations;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        std::cerr << "[discovery] Failed to create UDP socket\n";
        return result;
    }

    // Allow re-use and set send/receive timeouts
    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct timeval tv{};
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Multicast TTL
    unsigned char ttl = 4;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    sockaddr_in dest{};
    dest.sin_family      = AF_INET;
    dest.sin_port        = htons(SSDP_PORT);
    dest.sin_addr.s_addr = inet_addr(SSDP_ADDR);

    if (sendto(sock, SEARCH_MSG, strlen(SEARCH_MSG), 0,
               reinterpret_cast<sockaddr*>(&dest), sizeof(dest)) < 0) {
        std::cerr << "[discovery] sendto failed\n";
        close(sock);
        return result;
    }

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);

    char buf[2048];
    while (std::chrono::steady_clock::now() < deadline) {
        sockaddr_in src{};
        socklen_t src_len = sizeof(src);
        ssize_t n = recvfrom(sock, buf, sizeof(buf) - 1, 0,
                             reinterpret_cast<sockaddr*>(&src), &src_len);
        if (n <= 0) break;
        buf[n] = '\0';

        std::string reply(buf, n);
        std::string location = extractLocation(reply);
        if (location.empty()) continue;
        if (seen_locations.count(location)) continue;
        seen_locations.insert(location);

        SonosPlayer player;
        if (fetchDeviceDesc(location, player)) {
            std::cout << "[discovery] Found: " << player.name
                      << " (" << player.ip << ") model=" << player.model << "\n";
            result.push_back(std::move(player));
        }
    }

    close(sock);
    return result;
}
