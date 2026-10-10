// dump_events: runs a MidPluck engine (MONO or POLY) on WAV files and writes
// the note events as JSON, in the format of test/dump-events.mjs and
// test/poly-dump-events.mjs (docs/plugin-plan.md, sections 2 and 4), so that
// test/diff-events.mjs and test/score.py compare the C++ with the JS.
//
//   dump_events [options] <file.wav> [out.json]
//   dump_events [options] --guitarset <dir> --set <name> --takes <list.txt> --out <out.json>
//   dump_events [options] --mixdir <dir> --set <name> --takes <list.txt> --out <out.json>
//
// The take list has one take name per line (no JSON parser here); the WAV of
// take T is <dir>/audio_mono-pickup_mix/T_mix.wav for both --guitarset and
// --mixdir (the layout test/poly-dump-events.mjs and test/poly/make_mixes.py use).
#include "dump_common.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace midpluck::dump;

    const char* const kUsage =
        "usage: dump_events [options] <file.wav> [out.json]\n"
        "       dump_events [options] --guitarset <dir> | --mixdir <dir>  --set <name> --takes <list.txt> --out <out.json>\n"
        "options:\n"
        "  --mode mono|poly        engine (default mono)\n"
        "  --block N               host block size fed to the engine (default 128)\n"
        "  --stamp block|sample    MONO: stamp at the end of the block (default, like the JS) or at the decision sample\n"
        "  --align live|scipy      POLY: causal stream (default) or hops aligned on scipy's zero-phase resampling\n"
        "  --dense                 POLY: dense decomposer (sparse 0, 15 iterations), the prototype's exact arithmetic\n"
        "  --tuning K=V,...        MONO: TUNING overrides\n"
        "  --decomp k=v,...        POLY: DECOMPOSER overrides\n"
        "  --rule k=v,...          POLY: NOTE_RULE overrides\n"
        "  --label NAME            JSON label (default mono / poly)\n"
        "  --takes FILE            one take name per line (UTF-8/UTF-16 BOM, CR and empty lines ignored)\n";

    struct UsageError : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    int parseInt (const std::string& name, const std::string& v)
    {
        std::size_t used = 0;
        int x = 0;
        try { x = std::stoi (v, &used); }
        catch (const std::exception&) { used = 0; }
        if (used == 0 || used != v.size())
            throw UsageError ("--" + name + ": not an integer: " + v);
        return x;
    }

    /** Parses argv into opt; returns the set of options given explicitly. */
    std::set<std::string> parseArgs (int argc, char** argv, Options& opt)
    {
        static const std::set<std::string> valued = {
            "mode", "block", "stamp", "align", "tuning", "decomp", "rule", "label",
            "guitarset", "mixdir", "set", "takes", "out"
        };
        std::set<std::string> given;
        for (int i = 1; i < argc; ++i)
        {
            const std::string a = argv[i];
            if (a == "-h" || a == "--help")
                throw UsageError ("");
            if (a.size() > 2 && a.compare (0, 2, "--") == 0)
            {
                const std::string name = a.substr (2);
                if (name == "dense")
                {
                    opt.dense = true;
                    given.insert (name);
                    continue;
                }
                if (valued.count (name) == 0)
                    throw UsageError ("unknown option " + a);
                if (i + 1 >= argc)
                    throw UsageError ("missing value after " + a);
                const std::string v = argv[++i];
                given.insert (name);
                if (name == "mode") opt.mode = v;
                else if (name == "block") opt.block = parseInt (name, v);
                else if (name == "stamp") opt.stamp = v;
                else if (name == "align") opt.align = v;
                else if (name == "tuning") opt.tuning = v;
                else if (name == "decomp") opt.decomp = v;
                else if (name == "rule") opt.rule = v;
                else if (name == "label") opt.label = v;
                else if (name == "guitarset") opt.guitarset = v;
                else if (name == "mixdir") opt.mixdir = v;
                else if (name == "set") opt.set = v;
                else if (name == "takes") opt.takes = v;
                else if (name == "out") opt.out = v;
            }
            else
            {
                opt.positional.push_back (a);
            }
        }

        if (opt.mode != "mono" && opt.mode != "poly")
            throw UsageError ("--mode must be mono or poly, not " + opt.mode);
        if (opt.block < 1)
            throw UsageError ("--block must be a positive number of samples");
        if (opt.stamp != "block" && opt.stamp != "sample")
            throw UsageError ("--stamp must be block or sample, not " + opt.stamp);
        if (opt.align != "live" && opt.align != "scipy")
            throw UsageError ("--align must be live or scipy, not " + opt.align);
        // an option of the other engine is a mistake (e.g. --mode mono --dense), not something to ignore
        const bool poly = opt.mode == "poly";
        for (const char* o : { "stamp", "tuning" })
            if (poly && given.count (o)) throw UsageError (std::string ("--") + o + " applies to --mode mono only");
        for (const char* o : { "align", "dense", "decomp", "rule" })
            if (! poly && given.count (o)) throw UsageError (std::string ("--") + o + " applies to --mode poly only");

        const bool isSet = ! opt.guitarset.empty() || ! opt.mixdir.empty();
        if (! opt.guitarset.empty() && ! opt.mixdir.empty())
            throw UsageError ("give --guitarset or --mixdir, not both");
        if (isSet)
        {
            if (! opt.positional.empty())
                throw UsageError ("no WAV file arguments with --guitarset / --mixdir");
            if (opt.takes.empty())
                throw UsageError ("--takes <file> (one take name per line) is required with --guitarset / --mixdir");
        }
        else if (opt.positional.empty() || opt.positional.size() > 2)
        {
            throw UsageError ("give one WAV file (and optionally the output JSON), or --guitarset / --mixdir");
        }
        return given;
    }

    std::string utf8 (const std::filesystem::path& p)
    {
        const auto s = p.u8string();
        return std::string (s.begin(), s.end());
    }

    /** <dir>/audio_mono-pickup_mix/<take>_mix.wav */
    std::string takeWav (const std::string& dir, const std::string& take)
    {
        return utf8 (std::filesystem::u8path (dir) / "audio_mono-pickup_mix" / std::filesystem::u8path (take + "_mix.wav"));
    }

    bool endsWithNoCase (const std::string& s, const std::string& suffix)
    {
        if (s.size() < suffix.size()) return false;
        for (std::size_t i = 0; i < suffix.size(); ++i)
        {
            char a = s[s.size() - suffix.size() + i], b = suffix[i];
            if (a >= 'A' && a <= 'Z') a = static_cast<char> (a - 'A' + 'a');
            if (a != b) return false;
        }
        return true;
    }

    std::string baseName (const std::string& path)
    {
        return utf8 (std::filesystem::u8path (path).filename());
    }

    /** basename without .wav (any case), like basename(p).replace(/\.wav$/i, ''). */
    std::string stripWav (const std::string& name)
    {
        return endsWithNoCase (name, ".wav") ? name.substr (0, name.size() - 4) : name;
    }

    /** Take name of a single file: basename(p).replace(/(_mix)?\.wav$/i, ''). */
    std::string takeName (const std::string& path)
    {
        std::string n = baseName (path);
        if (endsWithNoCase (n, "_mix.wav")) return n.substr (0, n.size() - 8);
        return stripWav (n);
    }

    struct Stats
    {
        double mean = 0, p99 = 0, max = 0;
        bool valid = false;
    };

    /** mean, p99 (sorted[min(n-1, floor(0.99 n))], as poly-dump-events.mjs) and max. */
    Stats stats (const std::vector<double>& v)
    {
        Stats s;
        if (v.empty()) return s;
        std::vector<double> sorted (v);
        std::sort (sorted.begin(), sorted.end());
        double sum = 0;
        for (const double x : v) sum += x;
        s.mean = sum / static_cast<double> (v.size());
        const std::size_t k = std::min (sorted.size() - 1, static_cast<std::size_t> (0.99 * static_cast<double> (sorted.size())));
        s.p99 = sorted[k];
        s.max = sorted.back();
        s.valid = true;
        return s;
    }

    struct TakeCost
    {
        Stats hop, nmf;
        std::size_t hops = 0;
        double engineSeconds = 0, audioSeconds = 0;
    };

    void printTake (const Options& opt, const Take& take, const TakeCost& c)
    {
        std::fprintf (stderr, "%s: %zu notes", take.name.c_str(), take.events.size());
        if (opt.mode == "poly")
        {
            std::fprintf (stderr, ", %zu hops", c.hops);
            if (c.hop.valid)
                std::fprintf (stderr, ", hop %.3f ms mean / %.3f p99 / %.2f max", c.hop.mean, c.hop.p99, c.hop.max);
            if (c.nmf.valid)
                std::fprintf (stderr, ", NMF %.3f ms mean / %.3f p99 / %.2f max", c.nmf.mean, c.nmf.p99, c.nmf.max);
        }
        std::fprintf (stderr, ", engine %.3f s for %.3f s of audio (%.2f %% of one core)\n",
                      c.engineSeconds, c.audioSeconds,
                      c.audioSeconds > 0 ? 100.0 * c.engineSeconds / c.audioSeconds : 0.0);
    }

    int run (int argc, char** argv)
    {
        Options opt;
        parseArgs (argc, argv, opt);
        const bool poly = opt.mode == "poly";
        const bool isSet = ! opt.guitarset.empty() || ! opt.mixdir.empty();

        std::vector<std::pair<std::string, std::string>> jobs;   // take name, WAV path
        std::string outPath;
        if (isSet)
        {
            const std::string dir = ! opt.guitarset.empty() ? opt.guitarset : opt.mixdir;
            const auto list = readTakes (opt.takes);
            if (list.empty())
                throw std::runtime_error ("no take names in " + opt.takes);
            for (const auto& t : list)
                jobs.emplace_back (t, takeWav (dir, t));
            const std::string label = ! opt.label.empty() ? opt.label : (poly ? "poly" : "mono");
            outPath = ! opt.out.empty() ? opt.out : "events-" + label + "-" + opt.set + ".json";
        }
        else
        {
            jobs.emplace_back (takeName (opt.positional[0]), opt.positional[0]);
            outPath = opt.positional.size() > 1 ? opt.positional[1]
                    : ! opt.out.empty()          ? opt.out
                                                 : stripWav (baseName (opt.positional[0])) + (poly ? ".poly.json" : ".events.json");
        }

        std::vector<Take> takes;
        std::vector<TakeCost> costs;
        takes.reserve (jobs.size());
        for (const auto& job : jobs)
        {
            const Wav wav = readWav (job.second);
            Cost cost;
            const auto t0 = std::chrono::steady_clock::now();
            Take take = poly ? trackPoly (wav, opt, cost) : trackMono (wav, opt, cost);
            const double wall = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            take.name = job.first;

            TakeCost c;
            c.hop = stats (cost.hopMs);
            c.nmf = stats (cost.nmfMs);
            c.hops = cost.hopMs.size();
            c.engineSeconds = cost.totalSeconds > 0 ? cost.totalSeconds : wall;
            c.audioSeconds = wav.rate > 0 ? static_cast<double> (wav.samples.size()) / wav.rate : 0.0;
            printTake (opt, take, c);
            takes.push_back (std::move (take));
            costs.push_back (c);
        }

        writeJson (outPath, opt, takes);

        std::size_t notes = 0;
        double engine = 0, audio = 0, hopMean = 0, nmfMean = 0, hopMax = 0, nmfMax = 0;
        int withHop = 0, withNmf = 0;
        for (std::size_t i = 0; i < takes.size(); ++i)
        {
            notes += takes[i].events.size();
            engine += costs[i].engineSeconds;
            audio += costs[i].audioSeconds;
            if (costs[i].hop.valid) { hopMean += costs[i].hop.mean; hopMax = std::max (hopMax, costs[i].hop.max); ++withHop; }
            if (costs[i].nmf.valid) { nmfMean += costs[i].nmf.mean; nmfMax = std::max (nmfMax, costs[i].nmf.max); ++withNmf; }
        }
        std::fprintf (stderr, "wrote %s: %zu take(s), %zu notes; engine %.3f s for %.3f s of audio (%.2f %% of one core)",
                      outPath.c_str(), takes.size(), notes, engine, audio, audio > 0 ? 100.0 * engine / audio : 0.0);
        if (poly)
        {
            // the mean over the set is the mean of the per-take means, like poly-dump-events.mjs
            if (withHop) std::fprintf (stderr, "; hop %.3f ms mean, %.2f max", hopMean / withHop, hopMax);
            if (withNmf) std::fprintf (stderr, "; NMF %.3f ms mean, %.2f max", nmfMean / withNmf, nmfMax);
            std::fprintf (stderr, " (budget %.3f ms per hop)", 1000.0 * 64.0 / 24000.0);
        }
        std::fprintf (stderr, "\n");
        return 0;
    }
}

int main (int argc, char** argv)
{
    try
    {
        return run (argc, argv);
    }
    catch (const UsageError& e)
    {
        if (e.what()[0] != '\0') std::fprintf (stderr, "dump_events: %s\n", e.what());
        std::fputs (kUsage, stderr);
        return e.what()[0] != '\0' ? 2 : 0;
    }
    catch (const std::exception& e)
    {
        std::fprintf (stderr, "dump_events: %s\n", e.what());
        return 1;
    }
}
