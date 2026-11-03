#pragma once
#include <string>

namespace audio_engine {
inline bool IsValidSpeechText(const std::string& s) {
    if (s.empty() || s.size() > 4096 || s.find_first_not_of(" \t\r\n") == std::string::npos) return false;
    for (std::size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i++]);
        if (c < 0x80) { if (c < 0x20 && c != '\n' && c != '\r' && c != '\t') return false; continue; }
        unsigned cp = 0, n = 0, minimum = 0;
        if (c >= 0xc2 && c <= 0xdf) { cp = c & 31; n = 1; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { cp = c & 15; n = 2; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { cp = c & 7; n = 3; minimum = 0x10000; }
        else return false;
        if (i + n > s.size()) return false;
        while (n--) { c = static_cast<unsigned char>(s[i++]); if ((c & 0xc0) != 0x80) return false; cp = (cp << 6) | (c & 63); }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
    }
    return true;
}
} // namespace audio_engine
