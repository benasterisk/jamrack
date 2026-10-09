// dump_events, shared pieces: the WAV reader (same conversion as readWav in
// test/dump-events.mjs), the take list reader and the JSON writer (same
// structure as the JS dumps, numbers with 17 significant digits).
#include "dump_common.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace midpluck::dump
{
    namespace
    {
        std::filesystem::path toPath (const std::string& utf8)
        {
            return std::filesystem::u8path (utf8);
        }

        std::vector<unsigned char> readFile (const std::string& path)
        {
            std::ifstream in (toPath (path), std::ios::binary);
            if (! in)
                throw std::runtime_error ("cannot open " + path);
            return std::vector<unsigned char> ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        }

        std::uint32_t u32le (const std::vector<unsigned char>& b, std::size_t o)
        {
            return static_cast<std::uint32_t> (b[o]) | (static_cast<std::uint32_t> (b[o + 1]) << 8)
                 | (static_cast<std::uint32_t> (b[o + 2]) << 16) | (static_cast<std::uint32_t> (b[o + 3]) << 24);
        }

        std::uint16_t u16le (const std::vector<unsigned char>& b, std::size_t o)
        {
            return static_cast<std::uint16_t> (b[o] | (b[o + 1] << 8));
        }

        /** Appends one Unicode code point as UTF-8. */
        void putUtf8 (std::string& s, std::uint32_t cp)
        {
            if (cp < 0x80) s += static_cast<char> (cp);
            else if (cp < 0x800) { s += static_cast<char> (0xC0 | (cp >> 6)); s += static_cast<char> (0x80 | (cp & 0x3F)); }
            else if (cp < 0x10000) { s += static_cast<char> (0xE0 | (cp >> 12)); s += static_cast<char> (0x80 | ((cp >> 6) & 0x3F)); s += static_cast<char> (0x80 | (cp & 0x3F)); }
            else { s += static_cast<char> (0xF0 | (cp >> 18)); s += static_cast<char> (0x80 | ((cp >> 12) & 0x3F)); s += static_cast<char> (0x80 | ((cp >> 6) & 0x3F)); s += static_cast<char> (0x80 | (cp & 0x3F)); }
        }

        /** UTF-16 (little or big endian, BOM already skipped) to UTF-8. */
        std::string utf16ToUtf8 (const std::vector<unsigned char>& b, std::size_t start, bool bigEndian)
        {
            std::string out;
            auto unit = [&] (std::size_t o) -> std::uint32_t {
                return bigEndian ? static_cast<std::uint32_t> ((b[o] << 8) | b[o + 1])
                                 : static_cast<std::uint32_t> (b[o] | (b[o + 1] << 8));
            };
            for (std::size_t o = start; o + 1 < b.size(); o += 2)
            {
                std::uint32_t u = unit (o);
                if (u >= 0xD800 && u <= 0xDBFF && o + 3 < b.size())
                {
                    const std::uint32_t lo = unit (o + 2);
                    if (lo >= 0xDC00 && lo <= 0xDFFF)
                    {
                        u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                        o += 2;
                    }
                }
                putUtf8 (out, u);
            }
            return out;
        }

        void putString (std::FILE* f, const std::string& s)
        {
            std::fputc ('"', f);
            for (const unsigned char c : s)
            {
                switch (c)
                {
                    case '"':  std::fputs ("\\\"", f); break;
                    case '\\': std::fputs ("\\\\", f); break;
                    case '\b': std::fputs ("\\b", f); break;
                    case '\f': std::fputs ("\\f", f); break;
                    case '\n': std::fputs ("\\n", f); break;
                    case '\r': std::fputs ("\\r", f); break;
                    case '\t': std::fputs ("\\t", f); break;
                    default:
                        if (c < 0x20) std::fprintf (f, "\\u%04x", c);
                        else std::fputc (static_cast<int> (c), f);
                }
            }
            std::fputc ('"', f);
        }

        /** A JS number as JSON: 17 significant digits; NaN and infinities become null like JSON.stringify. */
        void putNumber (std::FILE* f, double x)
        {
            if (! std::isfinite (x)) { std::fputs ("null", f); return; }
            std::fprintf (f, "%.17g", x);
        }
    }

    Wav readWav (const std::string& path)
    {
        // Mirrors readWav() of test/dump-events.mjs chunk for chunk; where the
        // JS would throw on an out-of-range DataView read, this throws too.
        const std::vector<unsigned char> b = readFile (path);
        const std::uint64_t len = b.size();
        std::uint64_t o = 12;
        bool haveFmt = false, haveData = false;
        std::uint16_t format = 0, channels = 0, bits = 0;
        std::uint32_t rate = 0;
        std::uint64_t dataStart = 0, dataSize = 0;
        while (o + 8 <= len)
        {
            const std::string id (reinterpret_cast<const char*> (&b[static_cast<std::size_t> (o)]), 4);
            const std::uint64_t size = u32le (b, static_cast<std::size_t> (o + 4));
            if (id == "fmt ")
            {
                if (o + 24 > len)
                    throw std::runtime_error ("unsupported wav (truncated fmt chunk): " + path);
                format   = u16le (b, static_cast<std::size_t> (o + 8));
                channels = u16le (b, static_cast<std::size_t> (o + 10));
                rate     = u32le (b, static_cast<std::size_t> (o + 12));
                bits     = u16le (b, static_cast<std::size_t> (o + 22));
                haveFmt = true;
            }
            if (id == "data")
            {
                dataStart = o + 8;
                dataSize = std::min (size, len - o - 8);
                haveData = true;
                break;
            }
            o += 8 + size + (size & 1);
        }
        if (! haveFmt || ! haveData || format != 1 || bits != 16 || channels == 0)
            throw std::runtime_error ("unsupported wav: " + path);

        Wav wav;
        wav.rate = static_cast<int> (rate);
        const std::uint64_t n = dataSize / 2 / channels;     // Math.floor(size / 2 / channels)
        wav.samples.resize (static_cast<std::size_t> (n));
        for (std::uint64_t i = 0; i < n; ++i)
        {
            const std::size_t at = static_cast<std::size_t> (dataStart + i * 2 * channels);
            const auto s = static_cast<std::int16_t> (u16le (b, at));
            wav.samples[static_cast<std::size_t> (i)] = static_cast<float> (static_cast<double> (s) / 32768.0);
        }
        return wav;
    }

    std::vector<std::string> readTakes (const std::string& path)
    {
        const std::vector<unsigned char> b = readFile (path);
        std::string text;
        if (b.size() >= 2 && b[0] == 0xFF && b[1] == 0xFE)
            text = utf16ToUtf8 (b, 2, false);
        else if (b.size() >= 2 && b[0] == 0xFE && b[1] == 0xFF)
            text = utf16ToUtf8 (b, 2, true);
        else if (b.size() >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF)
            text.assign (b.begin() + 3, b.end());
        else
            text.assign (b.begin(), b.end());

        std::vector<std::string> takes;
        std::istringstream lines (text);
        std::string line;
        while (std::getline (lines, line))
        {
            const auto first = line.find_first_not_of (" \t\r");
            if (first == std::string::npos)
                continue;
            const auto last = line.find_last_not_of (" \t\r");
            takes.push_back (line.substr (first, last - first + 1));
        }
        return takes;
    }

    void writeJson (const std::string& path, const Options& opt, const std::vector<Take>& takes)
    {
        // Same keys, in the same order, as the JS dumps:
        //   MONO  { label, source, takes, set? }          (test/dump-events.mjs)
        //   POLY  { label, source, align, takes, set? }   (test/poly-dump-events.mjs)
        const bool poly = opt.mode == "poly";
        const bool isSet = ! opt.guitarset.empty() || ! opt.mixdir.empty();
        const std::string label = ! opt.label.empty() ? opt.label : (poly ? "poly" : "mono");
        const std::string source = poly ? "plugin/dsp (C++ port of js/audio/guitar/poly/engine.js)"
                                        : "plugin/dsp (C++ port of js/audio/guitar/tracker.js)";

        std::FILE* f = nullptr;
#ifdef _WIN32
        if (_wfopen_s (&f, toPath (path).wstring().c_str(), L"wb") != 0) f = nullptr;
#else
        f = std::fopen (path.c_str(), "wb");
#endif
        if (f == nullptr)
            throw std::runtime_error ("cannot write " + path);

        std::fputs ("{\"label\":", f);
        putString (f, label);
        std::fputs (",\"source\":", f);
        putString (f, source);
        if (poly)
        {
            std::fputs (",\"align\":", f);
            putString (f, opt.align);
        }
        std::fputs (",\"takes\":{", f);
        for (std::size_t t = 0; t < takes.size(); ++t)
        {
            const Take& take = takes[t];
            if (t) std::fputc (',', f);
            putString (f, take.name);
            std::fputs (":{\"duration\":", f);
            putNumber (f, take.duration);
            std::fputs (",\"events\":[", f);
            for (std::size_t i = 0; i < take.events.size(); ++i)
            {
                const Note& n = take.events[i];
                if (i) std::fputc (',', f);
                std::fputs ("{\"onset\":", f);
                putNumber (f, n.onset);
                std::fputs (",\"offset\":", f);
                putNumber (f, n.offset);
                std::fprintf (f, ",\"midi\":%d,\"velocity\":", n.midi);
                putNumber (f, n.velocity);
                std::fputs (",\"why\":", f);
                putString (f, n.why);
                std::fputc ('}', f);
            }
            std::fputs ("]}", f);
        }
        std::fputc ('}', f);
        if (isSet)
        {
            std::fputs (",\"set\":", f);
            putString (f, opt.set);
        }
        std::fputc ('}', f);
        const bool bad = std::ferror (f) != 0;
        if (std::fclose (f) != 0 || bad)
            throw std::runtime_error ("error while writing " + path);
    }
}
