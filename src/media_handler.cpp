#include "media_handler.h"
#include "xml_utils.h"

#include <httplib.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace fs = std::filesystem;

// ── MIME type lookup ──────────────────────────────────────────────────────────
static std::string mimeType(const std::string& ext) {
    if (ext == ".mp3")  return "audio/mpeg";
    if (ext == ".flac") return "audio/flac";
    if (ext == ".wav")  return "audio/wav";
    if (ext == ".aac")  return "audio/aac";
    if (ext == ".ogg")  return "audio/ogg";
    return "application/octet-stream";
}

// ── Parse "bytes=start-end" Range header ─────────────────────────────────────
static bool parseRange(const std::string& range_header,
                       long long file_size,
                       long long& out_start, long long& out_end) {
    // Format: "bytes=START-END"
    if (range_header.rfind("bytes=", 0) != 0) return false;
    std::string spec = range_header.substr(6);
    size_t dash = spec.find('-');
    if (dash == std::string::npos) return false;

    std::string s_start = spec.substr(0, dash);
    std::string s_end   = spec.substr(dash + 1);

    if (s_start.empty()) {
        // suffix-range: bytes=-N  (last N bytes)
        long long suffix = std::stoll(s_end);
        out_start = file_size - suffix;
        out_end   = file_size - 1;
    } else {
        out_start = std::stoll(s_start);
        out_end   = s_end.empty() ? file_size - 1 : std::stoll(s_end);
    }

    if (out_start < 0 || out_start > file_size - 1) return false;
    if (out_end   > file_size - 1) out_end = file_size - 1;
    return out_start <= out_end;
}

// ── Register routes ───────────────────────────────────────────────────────────
void registerMediaRoutes(httplib::Server& svr, const Config& cfg) {

    // GET /media/path — stream the file, with Range support
    // HEAD requests are handled automatically by cpp-httplib for any GET route
    svr.Get(R"(/media/(.+))", [&cfg](const httplib::Request& req,
                                     httplib::Response& res) {
        std::string rel  = urlDecode(req.matches[1].str());
        fs::path    full = fs::path(cfg.media_dir) / rel;

        std::cerr << "[media] request: " << rel << "\n";

        // Security: reject path traversal
        auto canon = fs::weakly_canonical(full);
        auto base  = fs::weakly_canonical(cfg.media_dir);
        if (canon.string().rfind(base.string(), 0) != 0) {
            res.status = 403;
            return;
        }

        std::error_code ec;
        if (!fs::exists(full, ec) || !fs::is_regular_file(full, ec)) {
            std::cerr << "[media] not found: " << full << "\n";
            res.status = 404;
            return;
        }

        auto file_size = static_cast<long long>(fs::file_size(full, ec));
        if (ec) { res.status = 500; return; }

        std::string ext = full.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        std::string mime = mimeType(ext);

        // Open file (shared_ptr so the content_provider lambda keeps it alive)
        auto file = std::make_shared<std::ifstream>(full.string(),
                                                    std::ios::binary);
        if (!file->is_open()) { res.status = 500; return; }

        long long range_start = 0;
        long long range_end   = file_size - 1;
        res.set_header("Accept-Ranges", "bytes");

        std::string range_hdr = req.get_header_value("Range");
        if (!range_hdr.empty()) {
            if (!parseRange(range_hdr, file_size, range_start, range_end)) {
                res.status = 416;
                res.set_header("Content-Range",
                               "bytes */" + std::to_string(file_size));
                return;
            }
            // For range requests: set status=206 but do NOT set Content-Range
            // manually. httplib generates it from req.ranges + content_length_
            // (= file_size below), which produces the correct
            // "bytes start-end/file_size" header without duplicates.
            res.status = 206;
        }

        // Single streaming provider for both full-file (range_start=0) and
        // range requests. httplib calls the provider with offset going from 0
        // to (range_length - 1); we add range_start so the reads land at the
        // right position in the file.
        // Passing file_size as content_length_ lets httplib compute correct
        // Content-Range and Content-Length headers for range requests.
        res.set_content_provider(
            static_cast<size_t>(file_size),
            mime,
            [file, range_start](size_t offset, size_t length,
                                httplib::DataSink& sink) -> bool {
                const size_t CHUNK = 65536;
                std::vector<char> buf(std::min(length, CHUNK));
                file->seekg(static_cast<std::streamoff>(range_start + offset));
                file->read(buf.data(), static_cast<std::streamsize>(buf.size()));
                auto n = file->gcount();
                if (n > 0) {
                    sink.write(buf.data(), static_cast<size_t>(n));
                }
                return true;
            });
    });
}
