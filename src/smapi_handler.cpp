#include "smapi_handler.h"
#include "xml_utils.h"

#include <tinyxml2.h>
#include <algorithm>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

// ── Constructor ───────────────────────────────────────────────────────────────
SMAPIHandler::SMAPIHandler(Config& cfg) : cfg_(cfg) {}

// ── Public entry point ────────────────────────────────────────────────────────
std::string SMAPIHandler::handle(const std::string& soap_body,
                                 const std::string& soap_action) {
    // SOAPAction: "http://www.sonos.com/Services/1.1#getMetadata"
    std::string method;
    auto hash = soap_action.find('#');
    if (hash != std::string::npos) {
        method = soap_action.substr(hash + 1);
        if (!method.empty() && method.front() == '"') method = method.substr(1);
        if (!method.empty() && method.back()  == '"') method.pop_back();
    }

    std::cout << "[smapi] method=" << method << "\n";

    tinyxml2::XMLDocument doc;
    if (doc.Parse(soap_body.c_str()) != tinyxml2::XML_SUCCESS) {
        std::cerr << "[smapi] Failed to parse SOAP body (len=" << soap_body.size()
                  << "): [" << soap_body.substr(0, 400) << "]\n";
        return soapFault("Client", "Malformed SOAP request");
    }

    auto* envelope  = doc.FirstChildElement();
    auto* body_el   = findByLocalName(envelope, "Body");
    auto* method_el = body_el ? body_el->FirstChildElement() : nullptr;

    if (!method_el) return soapFault("Client", "Empty SOAP body");

    try {
        if (method == "getMetadata") {
            return getMetadata(
                childText(method_el, "id", "root"),
                atoi(childText(method_el, "index", "0")),
                atoi(childText(method_el, "count", "100")));
        }
        if (method == "getMediaMetadata") {
            return getMediaMetadata(childText(method_el, "id", ""));
        }
        if (method == "getMediaURI") {
            return getMediaURI(childText(method_el, "id", ""));
        }
        if (method == "getLastUpdate") {
            return getLastUpdate();
        }
        if (method == "search") {
            return search(
                childText(method_el, "id", "root"),
                childText(method_el, "term", ""),
                atoi(childText(method_el, "index", "0")),
                atoi(childText(method_el, "count", "100")));
        }
        // Called by Sonos Voice Control: "Hey Sonos, play boygenius"
        if (method == "getMatchingSonosMusicObjects") {
            return getMatchingSonosMusicObjects(
                childText(method_el, "term", ""),
                atoi(childText(method_el, "index", "0")),
                atoi(childText(method_el, "count", "100")));
        }
        if (method == "getExtendedMetadata") {
            return getExtendedMetadata(childText(method_el, "id", ""));
        }
        if (method == "reportAccountAction") {
            return reportAccountAction(childText(method_el, "type", ""));
        }
        // Called during service onboarding (even for Anonymous services on some firmware)
        if (method == "getAppLink") {
            return getAppLink(
                childText(method_el, "householdId", ""),
                childText(method_el, "hardware",    ""));
        }

        std::cerr << "[smapi] Unhandled method: " << method << "\n";
        return soapFault("Client", "Method not supported: " + method);
    } catch (const std::exception& e) {
        std::cerr << "[smapi] Exception in " << method << ": " << e.what() << "\n";
        return soapFault("Server", e.what());
    }
}

// ── getMetadata ───────────────────────────────────────────────────────────────
std::string SMAPIHandler::getMetadata(const std::string& id,
                                       int index, int count) {
    std::string items;
    int total = 0;

    if (id == "root") {
        auto folders = listFolders();
        total = static_cast<int>(folders.size());
        int end = std::min(index + count, total);
        for (int i = index; i < end; ++i) items += folderXml(folders[i]);
        return wrapResult("getMetadata", index, end - index, total, items);
    }

    if (id.rfind("folder:", 0) == 0) {
        std::string folder = id.substr(7);
        auto tracks = listTracks(folder);
        total = static_cast<int>(tracks.size());
        int end = std::min(index + count, total);
        for (int i = index; i < end; ++i) items += trackXml(tracks[i]);
        return wrapResult("getMetadata", index, end - index, total, items);
    }

    return soapFault("Client", "Unknown container id: " + id);
}

// ── getMediaMetadata ──────────────────────────────────────────────────────────
std::string SMAPIHandler::getMediaMetadata(const std::string& id) {
    TrackInfo t = trackById(id);
    if (t.id.empty()) return soapFault("Client", "Track not found: " + id);

    return soapEnvelope(
        "<getMediaMetadataResponse xmlns=\"http://www.sonos.com/Services/1.1\">"
        "<getMediaMetadataResult>" + trackXml(t) + "</getMediaMetadataResult>"
        "</getMediaMetadataResponse>");
}

// ── getMediaURI ───────────────────────────────────────────────────────────────
// Returns the URL the Sonos player streams the audio from directly.
std::string SMAPIHandler::getMediaURI(const std::string& id) {
    TrackInfo t = trackById(id);
    if (t.id.empty()) return soapFault("Client", "Track not found: " + id);

    // id = "track:folder/file.mp3" — strip "track:" to get the URL path
    std::string rel_path = id.substr(6);
    std::string url = cfg_.media_url_base() + "/media/" + urlEncode(rel_path);

    return soapEnvelope(
        "<getMediaURIResponse xmlns=\"http://www.sonos.com/Services/1.1\">"
        "<getMediaURIResult>" + xmlEscape(url) + "</getMediaURIResult>"
        "</getMediaURIResponse>");
}

// ── getLastUpdate ─────────────────────────────────────────────────────────────
std::string SMAPIHandler::getLastUpdate() {
    std::string catalog_ver = "0";
    try {
        auto mtime = fs::last_write_time(cfg_.media_dir);
        catalog_ver = std::to_string(mtime.time_since_epoch().count());
    } catch (...) {}

    return soapEnvelope(
        "<getLastUpdateResponse xmlns=\"http://www.sonos.com/Services/1.1\">"
        "<getLastUpdateResult>"
        "<favorites>0</favorites>"
        "<catalog>" + catalog_ver + "</catalog>"
        "<pollInterval>60</pollInterval>"
        "</getLastUpdateResult>"
        "</getLastUpdateResponse>");
}

// ── search ────────────────────────────────────────────────────────────────────
std::string SMAPIHandler::search(const std::string& /*id*/,
                                  const std::string& term,
                                  int index, int count) {
    std::string lo_term = term;
    std::transform(lo_term.begin(), lo_term.end(), lo_term.begin(), ::tolower);

    // Search folders first, then tracks
    std::string items;
    int total = 0;
    int matched = 0;

    for (auto& folder : listFolders()) {
        std::string lo_name = folder.name;
        std::transform(lo_name.begin(), lo_name.end(), lo_name.begin(), ::tolower);
        if (lo_name.find(lo_term) != std::string::npos) {
            ++total;
            if (total > index && matched < count) {
                items += folderXml(folder);
                ++matched;
            }
        }
        for (auto& t : listTracks(folder.name)) {
            std::string lo_title = t.title;
            std::transform(lo_title.begin(), lo_title.end(), lo_title.begin(), ::tolower);
            if (lo_title.find(lo_term) != std::string::npos ||
                lo_name.find(lo_term)  != std::string::npos) {
                ++total;
                if (total > index && matched < count) {
                    items += trackXml(t);
                    ++matched;
                }
            }
        }
    }

    return wrapResult("search", index, matched, total, items);
}

// ── getMatchingSonosMusicObjects ──────────────────────────────────────────────
// Called by Sonos Voice Control when the user says "Hey Sonos, play <term>".
// Sonos expects a ranked list of matching objects — folders before tracks.
std::string SMAPIHandler::getMatchingSonosMusicObjects(const std::string& term,
                                                        int index, int count) {
    std::string lo_term = term;
    std::transform(lo_term.begin(), lo_term.end(), lo_term.begin(), ::tolower);

    std::vector<std::string> folder_matches;
    std::vector<std::string> track_matches;

    for (auto& folder : listFolders()) {
        std::string lo_name = folder.name;
        std::transform(lo_name.begin(), lo_name.end(), lo_name.begin(), ::tolower);

        bool folder_match = lo_name.find(lo_term) != std::string::npos;
        if (folder_match) folder_matches.push_back(folderXml(folder));

        for (auto& t : listTracks(folder.name)) {
            std::string lo_title = t.title;
            std::transform(lo_title.begin(), lo_title.end(), lo_title.begin(), ::tolower);
            if (lo_title.find(lo_term) != std::string::npos || folder_match) {
                track_matches.push_back(trackXml(t));
            }
        }
    }

    // Folders rank above individual tracks (album-level intent)
    std::vector<std::string> all;
    all.insert(all.end(), folder_matches.begin(), folder_matches.end());
    all.insert(all.end(), track_matches.begin(), track_matches.end());

    int total = static_cast<int>(all.size());
    int end   = std::min(index + count, total);
    std::string items;
    for (int i = index; i < end; ++i) items += all[i];

    return wrapResult("getMatchingSonosMusicObjects",
                      index, end - index, total, items);
}

// ── getExtendedMetadata ───────────────────────────────────────────────────────
// Called by Sonos before playback to get richer metadata.
// We return the same info as getMediaMetadata / getMetadata.
std::string SMAPIHandler::getExtendedMetadata(const std::string& id) {
    std::cout << "[smapi] getExtendedMetadata id=" << id << "\n";
    if (id.rfind("track:", 0) == 0) {
        TrackInfo t = trackById(id);
        if (t.id.empty()) return soapFault("Client", "Track not found: " + id);
        return soapEnvelope(
            "<getExtendedMetadataResponse xmlns=\"http://www.sonos.com/Services/1.1\">"
            "<getExtendedMetadataResult>"
            + trackXml(t) +
            "</getExtendedMetadataResult>"
            "</getExtendedMetadataResponse>");
    }
    if (id.rfind("folder:", 0) == 0) {
        std::string folder = id.substr(7);
        FolderInfo f;
        f.name    = folder;
        f.id      = id;
        f.path    = fs::path(cfg_.media_dir) / folder;
        f.art_url = albumArtUrl(folder);
        return soapEnvelope(
            "<getExtendedMetadataResponse xmlns=\"http://www.sonos.com/Services/1.1\">"
            "<getExtendedMetadataResult>"
            + folderXml(f) +
            "</getExtendedMetadataResult>"
            "</getExtendedMetadataResponse>");
    }
    return soapFault("Client", "Unknown id: " + id);
}

// ── reportAccountAction ───────────────────────────────────────────────────────
// Sonos calls this to report user actions (plays, skips, etc.). Acknowledge it.
std::string SMAPIHandler::reportAccountAction(const std::string& /*type*/) {
    return soapEnvelope(
        "<reportAccountActionResponse xmlns=\"http://www.sonos.com/Services/1.1\">"
        "<reportAccountActionResult/>"
        "</reportAccountActionResponse>");
}

// ── getAppLink ────────────────────────────────────────────────────────────────
// Sonos calls this during service onboarding to get an auth/setup URL.
// For Anonymous auth we return a minimal DeviceLink pointing at our base URL
// so the Sonos app can show a "no login needed" page and complete onboarding.
std::string SMAPIHandler::getAppLink(const std::string& /*householdId*/,
                                      const std::string& /*hardware*/) {
    std::string base = cfg_.public_base_url();
    return soapEnvelope(
        "<getAppLinkResponse xmlns=\"http://www.sonos.com/Services/1.1\">"
        "<getAppLinkResult>"
        "<authorizeAccount>"
        "<deviceLink>"
        "<regUrl>" + xmlEscape(base) + "/</regUrl>"
        "<linkCode>NONE</linkCode>"
        "<showLinkCode>false</showLinkCode>"
        "</deviceLink>"
        "</authorizeAccount>"
        "</getAppLinkResult>"
        "</getAppLinkResponse>");
}

// ── albumArtUrl ───────────────────────────────────────────────────────────────
// Convention: art file is named the same as the folder (e.g. boygenius.jpg).
// Falls back to cover.jpg or folder.jpg.
std::string SMAPIHandler::albumArtUrl(const std::string& folder) const {
    fs::path folder_path = fs::path(cfg_.media_dir) / folder;
    const std::vector<std::string> candidates = {
        folder + ".jpg", folder + ".png",
        "cover.jpg",     "cover.png",
        "folder.jpg",    "folder.png",
    };
    for (auto& name : candidates) {
        if (fs::exists(folder_path / name)) {
            return cfg_.public_base_url() + "/media/"
                   + urlEncode(folder) + "/" + urlEncode(name);
        }
    }
    return {};
}

// ── Library helpers ───────────────────────────────────────────────────────────
std::vector<FolderInfo> SMAPIHandler::listFolders() const {
    std::vector<FolderInfo> folders;
    std::error_code ec;
    for (auto& entry : fs::directory_iterator(cfg_.media_dir, ec)) {
        if (!entry.is_directory()) continue;
        FolderInfo f;
        f.name    = entry.path().filename().string();
        f.id      = "folder:" + f.name;
        f.path    = entry.path();
        f.art_url = albumArtUrl(f.name);
        folders.push_back(std::move(f));
    }
    std::sort(folders.begin(), folders.end(),
              [](const FolderInfo& a, const FolderInfo& b) {
                  return a.name < b.name;
              });
    return folders;
}

std::vector<TrackInfo> SMAPIHandler::listTracks(const std::string& folder) const {
    std::vector<TrackInfo> tracks;
    fs::path folder_path = fs::path(cfg_.media_dir) / folder;
    std::string art = albumArtUrl(folder);
    std::error_code ec;

    for (auto& entry : fs::directory_iterator(folder_path, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext != ".mp3" && ext != ".flac" && ext != ".wav" && ext != ".aac") continue;

        TrackInfo t;
        t.path    = entry.path().string();
        t.artist  = folder;
        t.album   = folder;
        t.title   = entry.path().stem().string();
        std::replace(t.title.begin(), t.title.end(), '_', ' ');
        t.id      = "track:" + folder + "/" + entry.path().filename().string();
        t.art_url = art;
        tracks.push_back(std::move(t));
    }
    std::sort(tracks.begin(), tracks.end(),
              [](const TrackInfo& a, const TrackInfo& b) {
                  return a.title < b.title;
              });
    return tracks;
}

TrackInfo SMAPIHandler::trackById(const std::string& smapi_id) const {
    if (smapi_id.rfind("track:", 0) != 0) return {};
    std::string rel   = smapi_id.substr(6);
    size_t slash      = rel.find('/');
    if (slash == std::string::npos) return {};
    std::string folder   = rel.substr(0, slash);
    std::string filename = rel.substr(slash + 1);

    TrackInfo t;
    t.id      = smapi_id;
    t.path    = (fs::path(cfg_.media_dir) / folder / filename).string();
    t.artist  = folder;
    t.album   = folder;
    t.title   = fs::path(filename).stem().string();
    std::replace(t.title.begin(), t.title.end(), '_', ' ');
    t.art_url = albumArtUrl(folder);

    if (!fs::exists(t.path)) return {};
    return t;
}

// ── XML builders ──────────────────────────────────────────────────────────────
std::string SMAPIHandler::trackXml(const TrackInfo& t) const {
    std::string duration_str = "0";
    std::error_code ec;
    auto sz = fs::file_size(t.path, ec);
    if (!ec && sz > 0) {
        long secs = static_cast<long>(sz) / 16000; // ~128kbps
        duration_str = std::to_string(secs > 0 ? secs : 1);
    }

    std::string art_elem = t.art_url.empty()
        ? ""
        : "<albumArtURI>" + xmlEscape(t.art_url) + "</albumArtURI>";

    // Determine MIME type from file extension
    std::string ext = fs::path(t.path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    std::string mime = "audio/mpeg";
    if (ext == ".flac") mime = "audio/flac";
    else if (ext == ".wav")  mime = "audio/wav";
    else if (ext == ".aac")  mime = "audio/aac";
    else if (ext == ".ogg")  mime = "audio/ogg";

    return "<mediaMetadata>"
           "<id>"    + xmlEscape(t.id)    + "</id>"
           "<itemType>track</itemType>"
           "<title>" + xmlEscape(t.title) + "</title>"
           "<trackMetadata>"
           "<artist>"   + xmlEscape(t.artist) + "</artist>"
           "<album>"    + xmlEscape(t.album)  + "</album>" +
           art_elem +
           "<duration>" + duration_str        + "</duration>"
           "</trackMetadata>"
           "<mimeType>" + mime               + "</mimeType>"
           "<canPlay>true</canPlay>"
           "</mediaMetadata>";
}

std::string SMAPIHandler::folderXml(const FolderInfo& f) const {
    std::string art_elem = f.art_url.empty()
        ? ""
        : "<albumArtURI>" + xmlEscape(f.art_url) + "</albumArtURI>";

    return "<mediaCollection>"
           "<id>"    + xmlEscape(f.id)   + "</id>"
           "<itemType>album</itemType>"
           "<title>" + xmlEscape(f.name) + "</title>" +
           art_elem +
           "<canPlay>true</canPlay>"
           "<canEnumerate>true</canEnumerate>"
           "<canAddToFavorites>false</canAddToFavorites>"
           "</mediaCollection>";
}

std::string SMAPIHandler::wrapResult(const std::string& method,
                                      int index, int count, int total,
                                      const std::string& items) const {
    return soapEnvelope(
        "<" + method + "Response xmlns=\"http://www.sonos.com/Services/1.1\">"
        "<" + method + "Result>"
        "<index>" + std::to_string(index) + "</index>"
        "<count>" + std::to_string(count) + "</count>"
        "<total>" + std::to_string(total) + "</total>" +
        items +
        "</" + method + "Result>"
        "</" + method + "Response>");
}
