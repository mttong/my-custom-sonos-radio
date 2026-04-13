#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include "config.h"

namespace fs = std::filesystem;

struct TrackInfo {
    std::string id;        // "track:folder/file.mp3"
    std::string title;
    std::string artist;    // derived from parent folder name
    std::string album;
    std::string path;      // absolute filesystem path
    std::string art_url;   // public HTTPS URL to cover art (may be empty)
};

struct FolderInfo {
    std::string id;        // "folder:dirname"
    std::string name;
    fs::path    path;
    std::string art_url;   // public HTTPS URL to cover art (may be empty)
};

// Implements the Sonos Music API (SMAPI) SOAP endpoint.
// Sonos players call this to browse, stream, and voice-search your local library.
class SMAPIHandler {
public:
    explicit SMAPIHandler(Config& cfg);

    // Entry point for POST /smapi. Returns SOAP XML response.
    std::string handle(const std::string& soap_body,
                       const std::string& soap_action);

private:
    Config& cfg_;

    // ── SMAPI method handlers ──────────────────────────────────────────────
    std::string getMetadata(const std::string& id, int index, int count);
    std::string getMediaMetadata(const std::string& id);
    std::string getMediaURI(const std::string& id);
    std::string getLastUpdate();
    std::string search(const std::string& id, const std::string& term,
                       int index, int count);
    // Powers "Hey Sonos, play boygenius" — Sonos Voice Control calls this
    std::string getMatchingSonosMusicObjects(const std::string& term,
                                             int index, int count);
    // Called during service onboarding — returns a no-op link for Anonymous auth
    std::string getAppLink(const std::string& householdId,
                           const std::string& hardware);
    // Called before playback — returns same metadata as getMediaMetadata/getMetadata
    std::string getExtendedMetadata(const std::string& id);
    // Reporting callback — acknowledge and ignore
    std::string reportAccountAction(const std::string& type);

    // ── Library helpers ────────────────────────────────────────────────────
    std::vector<FolderInfo> listFolders() const;
    std::vector<TrackInfo>  listTracks(const std::string& folder) const;
    TrackInfo               trackById(const std::string& smapi_id) const;

    // Returns public URL for cover art, or "" if none found.
    // Looks for {folder}/{folder}.jpg, then cover.jpg, then folder.jpg.
    std::string albumArtUrl(const std::string& folder) const;

    // ── XML builders ──────────────────────────────────────────────────────
    std::string trackXml(const TrackInfo& t) const;
    std::string folderXml(const FolderInfo& f) const;
    std::string wrapResult(const std::string& method,
                           int index, int count, int total,
                           const std::string& items) const;
};
