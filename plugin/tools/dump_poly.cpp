// dump_events, POLY part: midpluck::dump::trackPoly(), the C++ twin of
// trackNotesPoly() in test/poly-dump-events.mjs.
//
// Same steps as the JS harness: one bank for the whole run; per take a fresh
// PolyTracker fed at the analysis rate by a separate Resampler at the WAV rate;
// align 'scipy' drops the first round(halfLen / down) resampled samples (the
// hops then fall where scipy's zero-phase resample_poly put them), 'live' keeps
// the causal stream; note-on / note-off stamped (hop + 1) * 64 / 24000 s; a
// note-on for a midi still open closes it; flush at the end, still-open notes
// closed at the last hop; notes sorted by onset (stable).
//
// Options: --dense = DECOMPOSER { sparse: 0, iter: 15 } (the plan's
// DECOMP='{"sparse":0,"iter":15}'), then --decomp and --rule overrides
// "key=value,key=value" (a JSON object such as {"sparse":0,"iter":15} is read
// the same way). --decomp also takes autoEco=0|1 and agc=0|1.
//
// ECO: the engine's ECO switch reacts to the wall clock, so a dump made with it
// depends on the machine's load. In the shipped setting Node spends 0.5-0.6 ms
// per hop and ECO never engages (0 switches over the 36 GuitarSet takes); in
// dense mode Node spends 1.9-3.0 ms per hop and ECO DOES engage (16 switches on
// 00_BN1-129-Eb_solo on a loaded PC). Both harnesses therefore run with the
// switch OFF by default, which makes every dump a pure function of the WAV:
// test/poly-dump-events.mjs passes autoEco: false (--eco on restores it) and
// so does this file (--decomp autoEco=1 restores it). The ECO decomposer
// itself (iter 5, active 40) is still checked: --decomp active=40,iter=5 here
// and DECOMP='{"active":40,"iter":5}' on the JS side give the same notes.
//
// Cost: cost.hopMs / cost.nmfMs get the engine's own per-hop measurement
// (steady_clock around the whole hop and around the decomposer, as engine.js
// does with performance.now()), one value per hop; the JS prints the mean and
// p99 of its 8-hop exponential average and the raw max.

#include "dump_common.h"

#include "midpluck/events.h"
#include "midpluck/jsmath.h"
#include "midpluck/poly/engine.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace midpluck::dump
{
    namespace
    {
        using namespace midpluck::poly;

        constexpr double HOP_S = static_cast<double> (HOP) / SR;

        /** "k=v,k=v" or {"k":v,...} -> pairs. */
        std::vector<std::pair<std::string, std::string>> parsePairs (const std::string& text)
        {
            std::string s;
            for (char c : text)
                if (c != '{' && c != '}' && c != '"' && c != '\'' && c != ' ' && c != '\t') s += c;
            std::vector<std::pair<std::string, std::string>> out;
            size_t start = 0;
            while (start < s.size())
            {
                size_t end = s.find (',', start);
                if (end == std::string::npos) end = s.size();
                const std::string item = s.substr (start, end - start);
                if (! item.empty())
                {
                    const size_t eq = item.find_first_of ("=:");
                    if (eq == std::string::npos || eq == 0 || eq + 1 == item.size())
                        throw std::runtime_error ("override \"" + item + "\": expected key=value");
                    out.emplace_back (item.substr (0, eq), item.substr (eq + 1));
                }
                start = end + 1;
            }
            return out;
        }

        double toNumber (const std::string& key, const std::string& v)
        {
            if (v == "true") return 1;
            if (v == "false") return 0;
            char* end = nullptr;
            const double x = std::strtod (v.c_str(), &end);
            if (end == v.c_str() || *end != '\0')
                throw std::runtime_error ("override " + key + ": not a number: " + v);
            return x;
        }

        void applyDecomp (const std::string& text, PolyParams& p)
        {
            for (const auto& [k, v] : parsePairs (text))
            {
                const double x = toNumber (k, v);
                if (k == "active") p.decomposer.active = x;
                else if (k == "iter") p.decomposer.iter = x;
                else if (k == "lambda") p.decomposer.lambda = x;
                else if (k == "initSal") p.decomposer.initSal = x;
                else if (k == "sparse") p.decomposer.sparse = x;
                else if (k == "autoEco") p.autoEco = x != 0;
                else if (k == "agc") p.agc = x != 0;
                else throw std::runtime_error ("--decomp: unknown key " + k);
            }
        }

        void applyRule (const std::string& text, NoteRuleParams& r)
        {
            for (const auto& [k, v] : parsePairs (text))
            {
                const double x = toNumber (k, v);
                if (k == "absOn") r.absOn = x;
                else if (k == "frac") r.frac = x;
                else if (k == "riseX") r.riseX = x;
                else if (k == "fluxThr") r.fluxThr = x;
                else if (k == "riseDb") r.riseDb = x;
                else if (k == "releaseDb") r.releaseDb = x;
                else if (k == "offHops") r.offHops = x;
                else if (k == "windowHops") r.windowHops = x;
                else if (k == "refractory") r.refractory = x;
                else if (k == "octOdd") r.octOdd = x;
                else if (k == "octUp") r.octUp = x;
                else if (k == "maxVoices") r.maxVoices = x;
                else if (k == "agree") r.agree = x;
                else if (k == "minDelay") r.minDelay = x;
                else if (k == "lowGuard") r.lowGuard = x;
                else if (k == "harmUp") r.harmUp = x;
                else if (k == "gateDb") r.gateDb = x;
                else if (k == "silenceHops") r.silenceHops = x;
                else throw std::runtime_error ("--rule: unknown key " + k);
            }
        }

        const Bank& sharedBank()
        {
            static const Bank bank = buildBank (BankWindow::Medium, genericProfile());   // bank || buildBank('medium')
            return bank;
        }
    }

    Take trackPoly (const Wav& wav, const Options& opt, Cost& cost)
    {
        const Bank& bank = sharedBank();
        PolyParams params;
        params.autoEco = false;   // see the header comment
        if (opt.dense) { params.decomposer.sparse = 0; params.decomposer.iter = 15; }
        if (! opt.decomp.empty()) applyDecomp (opt.decomp, params);
        if (! opt.rule.empty()) applyRule (opt.rule, params.rule);
        if (opt.align != "scipy" && opt.align != "live")
            throw std::runtime_error ("--align must be scipy or live");

        const auto start = std::chrono::steady_clock::now();
        PolyTracker tr (SR, bank, params);          // fed at the analysis rate below
        Resampler rs (wav.rate, SR);
        const long long lag = opt.align == "scipy"
            ? static_cast<long long> (js::round (static_cast<double> (rs.halfLen) / static_cast<double> (rs.down)))
            : 0;

        Take take;
        take.duration = static_cast<double> (wav.samples.size()) / wav.rate;
        std::vector<Note>& notes = take.events;
        int open[128];
        for (int& o : open) o = -1;
        auto close = [&] (int midi, long long hop)
        {
            if (midi < 0 || midi > 127 || open[midi] < 0) return;
            notes[static_cast<size_t> (open[midi])].offset = static_cast<double> (hop + 1) * HOP_S;
            open[midi] = -1;
        };

        auto events = std::make_unique<EventList>();
        auto drain = [&]
        {
            if (events->dropped) throw std::runtime_error ("trackPoly: event list overflow");
            for (int i = 0; i < events->count; i++)
            {
                const Event& e = events->items[i];
                if (e.type == EventType::NoteOn)
                {
                    close (e.midi, e.frame);
                    Note n;
                    n.onset = static_cast<double> (e.frame + 1) * HOP_S;
                    n.offset = -1;
                    n.midi = e.midi;
                    n.velocity = e.vel;
                    n.why = e.why;
                    notes.push_back (n);
                    if (e.midi >= 0 && e.midi <= 127) open[e.midi] = static_cast<int> (notes.size()) - 1;
                }
                else if (e.type == EventType::NoteOff)
                {
                    close (e.midi, e.frame);
                }
            }
            events->clear();
        };

        // Analysis-rate samples go in by whole hops (64): the tracker's hop
        // counter starts with the stream, so each full chunk completes exactly
        // one hop and its cost can be read right after (results do not depend
        // on the chunking: the engine runs sample by sample).
        double buf[HOP];
        int n = 0;
        long long skipped = 0;
        auto feed = [&]
        {
            const long long before = tr.hop;
            tr.pushAnalysisRate (buf, n, *events);
            if (tr.hop != before)
            {
                cost.hopMs.push_back (tr.lastHopMs);
                cost.nmfMs.push_back (tr.lastNmfMs);
            }
            n = 0;
            drain();
        };
        const float* x = wav.samples.data();
        const size_t len = wav.samples.size();
        for (size_t i = 0; i < len; i++)
        {
            rs.pushSample (static_cast<double> (x[i]), [&] (double y)
            {
                if (skipped < lag) { skipped++; return; }
                buf[n++] = y;
                if (n == HOP) feed();
            });
        }
        if (n > 0) feed();
        const long long lastHop = tr.hop - 1;
        tr.flush (*events);
        drain();
        const double end = static_cast<double> (lastHop + 1) * HOP_S;
        for (Note& nt : notes) if (nt.offset < 0) nt.offset = end;
        std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.onset < b.onset; });
        cost.totalSeconds += std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
        return take;
    }
}
