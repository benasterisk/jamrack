// dump_events, MONO part: trackMono() runs dsp/mono_tracker on one WAV exactly
// the way trackNotes() of test/dump-events.mjs runs js/audio/guitar/tracker.js,
// so that test/diff-events.mjs can compare the two note for note.
//
// Time stamps (opt.stamp):
//   "block"  (default, what the JS does) the end of the host block that
//            produced the event: (o + chunk) / rate, blocks of opt.block samples.
//   "sample" the end of the input sample that completed the analysis frame
//            which decided the event: (o + sampleOffset + 1) / rate. "+ 1"
//            because the decision exists only once that sample has been
//            consumed, which is also what "block" means by the end of the
//            block: a frame completed by the last sample of a block gets the
//            same stamp in both modes, and "sample" is never later than "block".
#include "dump_common.h"

#include "midpluck/events.h"
#include "midpluck/mono_tracker.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>

namespace midpluck::dump
{
    namespace
    {
        std::string trim (const std::string& s)
        {
            const auto b = s.find_first_not_of (" \t\r\n");
            if (b == std::string::npos) return {};
            const auto e = s.find_last_not_of (" \t\r\n");
            return s.substr (b, e - b + 1);
        }

        /** "KEY=value,KEY=value" -> Tuning fields (JS: Object.assign(TUNING, JSON.parse(env.TUNING))). */
        void applyTuning (Tuning& t, const std::string& spec)
        {
            size_t pos = 0;
            while (pos <= spec.size())
            {
                const size_t comma = spec.find (',', pos);
                const std::string item = trim (spec.substr (pos, comma == std::string::npos ? std::string::npos : comma - pos));
                if (! item.empty())
                {
                    const size_t eq = item.find ('=');
                    if (eq == std::string::npos) throw std::runtime_error ("--tuning: expected KEY=value, got \"" + item + "\"");
                    const std::string key = trim (item.substr (0, eq));
                    const std::string val = trim (item.substr (eq + 1));
                    double v = 0;
                    if (val == "true") v = 1;
                    else if (val == "false") v = 0;
                    else
                    {
                        char* end = nullptr;
                        v = std::strtod (val.c_str(), &end);
                        if (val.empty() || end == nullptr || *end != '\0')
                            throw std::runtime_error ("--tuning: not a number for " + key + ": \"" + val + "\"");
                    }
                    if (! setTuningValue (t, key.c_str(), v))
                        throw std::runtime_error ("--tuning: unknown MONO TUNING key \"" + key + "\"");
                }
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
        }
    }

    Take trackMono (const Wav& wav, const Options& opt, Cost& cost)
    {
        if (wav.rate <= 0) throw std::runtime_error ("trackMono: bad sample rate");
        const bool stampSample = opt.stamp == "sample";
        if (! stampSample && opt.stamp != "block") throw std::runtime_error ("--stamp must be block or sample, got \"" + opt.stamp + "\"");
        const int block = std::max (1, opt.block);
        // The engine is fed sample by sample, so a host block may be cut into
        // shorter process() calls without changing any decision; the cut keeps
        // every call far below the EventList capacity.
        constexpr int maxChunk = 256;

        MonoTracker tr (static_cast<double> (wav.rate));
        if (! opt.tuning.empty()) applyTuning (tr.tuning, opt.tuning);

        auto events = std::make_unique<EventList>();
        Take take;
        const double rate = static_cast<double> (wav.rate);
        const long long total = static_cast<long long> (wav.samples.size());
        long open = -1;   // index of the open note in take.events, or -1
        double engineSeconds = 0;

        for (long long o = 0; o < total; o += block)
        {
            const long long chunk = std::min<long long> (block, total - o);
            const double tBlock = static_cast<double> (o + chunk) / rate;
            for (long long s = 0; s < chunk; s += maxChunk)
            {
                const int len = static_cast<int> (std::min<long long> (maxChunk, chunk - s));
                events->clear();
                const auto t0 = std::chrono::steady_clock::now();
                tr.process (wav.samples.data() + o + s, len, *events);
                engineSeconds += std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
                if (events->dropped != 0) throw std::runtime_error ("trackMono: event list overflow");

                for (int i = 0; i < events->count; i++)
                {
                    const Event& e = events->items[i];
                    const double t = stampSample ? static_cast<double> (o + s + e.sampleOffset + 1) / rate : tBlock;
                    if (e.type == EventType::NoteOn)
                    {
                        if (open >= 0) { take.events[static_cast<size_t> (open)].offset = t; open = -1; }   // the tracker sends 'off' first, but be safe
                        Note nt;
                        nt.onset = t;
                        nt.offset = -1;
                        nt.midi = e.midi;
                        nt.velocity = e.vel;
                        nt.why = e.why;
                        take.events.push_back (nt);
                        open = static_cast<long> (take.events.size()) - 1;
                    }
                    else if (e.type == EventType::NoteOff && open >= 0 && take.events[static_cast<size_t> (open)].midi == e.midi)
                    {
                        take.events[static_cast<size_t> (open)].offset = t;
                        open = -1;
                    }
                }
            }
        }

        const double end = static_cast<double> (total) / rate;
        for (Note& nt : take.events)
            if (nt.offset < 0) nt.offset = end;
        take.duration = end;
        cost.totalSeconds += engineSeconds;
        return take;
    }
}
