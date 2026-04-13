#pragma once
#include <string>
#include <tinyxml2.h>

// ── Namespace-aware element finder ───────────────────────────────────────────
// tinyxml2 stores element names with their prefix (e.g. "s:Body").
// These helpers find by local name regardless of prefix.

inline tinyxml2::XMLElement* findByLocalName(
    tinyxml2::XMLNode* parent, const char* localName)
{
    if (!parent) return nullptr;
    for (auto* child = parent->FirstChildElement();
         child; child = child->NextSiblingElement()) {
        const char* name = child->Name();
        const char* colon = strchr(name, ':');
        const char* local = colon ? colon + 1 : name;
        if (strcmp(local, localName) == 0) return child;
    }
    return nullptr;
}

inline const char* childText(tinyxml2::XMLElement* parent, const char* tag,
                              const char* def = "")
{
    if (!parent) return def;
    auto* child = parent->FirstChildElement(tag);
    if (!child) {
        // fallback: namespace-agnostic search
        for (auto* c = parent->FirstChildElement(); c;
             c = c->NextSiblingElement()) {
            const char* name = c->Name();
            const char* colon = strchr(name, ':');
            const char* local = colon ? colon + 1 : name;
            if (strcmp(local, tag) == 0) {
                const char* txt = c->GetText();
                return txt ? txt : def;
            }
        }
        return def;
    }
    const char* txt = child->GetText();
    return txt ? txt : def;
}

// ── SOAP envelope wrapper ─────────────────────────────────────────────────────
inline std::string soapEnvelope(const std::string& bodyContent) {
    return R"(<?xml version="1.0" encoding="utf-8"?>)"
           R"(<soap:Envelope xmlns:soap="http://schemas.xmlsoap.org/soap/envelope/">)"
           "<soap:Body>" +
           bodyContent +
           "</soap:Body>"
           "</soap:Envelope>";
}

inline std::string soapFault(const std::string& code, const std::string& msg) {
    return soapEnvelope(
        "<soap:Fault>"
        "<faultcode>" + code + "</faultcode>"
        "<faultstring>" + msg + "</faultstring>"
        "</soap:Fault>");
}

// ── Minimal XML escaping ──────────────────────────────────────────────────────
inline std::string xmlEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out += c;
        }
    }
    return out;
}

// ── URL encode/decode (for media paths) ──────────────────────────────────────
inline std::string urlEncode(const std::string& s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '/' || c == ':') {
            out += static_cast<char>(c);
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

inline std::string urlDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            char hex[3] = {s[i+1], s[i+2], '\0'};
            out += static_cast<char>(strtol(hex, nullptr, 16));
            i += 2;
        } else if (s[i] == '+') {
            out += ' ';
        } else {
            out += s[i];
        }
    }
    return out;
}
