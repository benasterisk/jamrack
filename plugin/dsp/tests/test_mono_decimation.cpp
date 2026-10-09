// MONO closed test: decimation factor and frame geometry at the host rates
// (tracker.js constructor and _setupYin), and the sample offsets of the
// events (one frame = 64 decimated samples = 64 * D input samples; an event
// is stamped with the input sample that completed its frame).
#include "mono_test_util.h"

using namespace midpluck;

namespace
{
    struct Expect { double sr; int D; double srD; int tmin, tmax, W, ring; };

    void countFrames (void* user) { ++*static_cast<long long*> (user); }
}

int main()
{
    // D = max(1, Math.round(sr / 24000)); tmin = max(2, floor(srD / 1400)); tmax = ceil(srD / 70);
    // W = max(512, Math.round(1.5 * tmax)); ring = smallest power of two >= 1024 and >= 2 (W + tmax)
    const Expect table[] = {
        { 22050, 1, 22050, 15, 315, 512, 2048 },
        { 44100, 2, 22050, 15, 315, 512, 2048 },
        { 48000, 2, 24000, 17, 343, 515, 2048 },   // Math.round(514.5) = 515 (halves go up)
        { 88200, 4, 22050, 15, 315, 512, 2048 },
        { 96000, 4, 24000, 17, 343, 515, 2048 },
        { 192000, 8, 24000, 17, 343, 515, 2048 },
    };
    for (const Expect& e : table)
    {
        MonoTracker tr (e.sr);
        MT_CHECK (tr.decimation() == e.D, "%.0f Hz: D = %d, expected %d", e.sr, tr.decimation(), e.D);
        MT_CHECK (tr.decimatedRate() == e.srD, "%.0f Hz: srD = %.3f, expected %.3f", e.sr, tr.decimatedRate(), e.srD);
        MT_CHECK (tr.frameMs() == MonoTracker::HOP / e.srD * 1000, "%.0f Hz: frameMs = %.17g", e.sr, tr.frameMs());
        MT_CHECK (tr.tmin() == e.tmin && tr.tmax() == e.tmax && tr.window() == e.W && tr.ringSize() == e.ring,
                  "%.0f Hz: tmin %d tmax %d W %d ring %d, expected %d %d %d %d", e.sr, tr.tmin(), tr.tmax(), tr.window(), tr.ringSize(), e.tmin, e.tmax, e.W, e.ring);
        std::printf ("%6.0f Hz: D = %d, srD = %.0f Hz, frame = %d input samples = %.4f ms, tmin %d, tmax %d, W %d, ring %d\n",
                     e.sr, tr.decimation(), tr.decimatedRate(), MonoTracker::HOP * tr.decimation(), tr.frameMs(), tr.tmin(), tr.tmax(), tr.window(), tr.ringSize());
    }

    // Frame cadence and event stamping, with block sizes that do and do not divide a frame.
    for (const double sr : { 44100.0, 48000.0, 96000.0 })
    {
        for (const int block : { 128, 100, 256, 37 })
        {
            MonoTracker tr (sr);
            long long frames = 0;
            tr.setTrace ([] (const MonoFrameTrace&, void* u) { countFrames (u); }, &frames);
            const int frameLen = MonoTracker::HOP * tr.decimation();
            const std::vector<float> sig = monotest::silence (sr, 1.0);
            auto list = std::make_unique<EventList>();
            int meters = 0;
            for (size_t o = 0; o < sig.size(); o += static_cast<size_t> (block))
            {
                const int len = static_cast<int> (std::min<size_t> (static_cast<size_t> (block), sig.size() - o));
                list->clear();
                tr.process (sig.data() + o, len, *list);
                for (int i = 0; i < list->count; i++)
                {
                    const Event& ev = list->items[i];
                    MT_CHECK (ev.type == EventType::Meter, "silence produced a non-meter event");
                    if (ev.type != EventType::Meter) continue;
                    ++meters;
                    // frame k is completed by the input sample of absolute index k * frameLen - 1
                    const long long abs = static_cast<long long> (o) + ev.sampleOffset;
                    MT_CHECK (ev.frame % MonoTracker::METER_EVERY == 0, "meter on frame %lld", ev.frame);
                    MT_CHECK (abs == ev.frame * frameLen - 1, "%.0f Hz / block %d: frame %lld stamped at sample %lld, expected %lld",
                              sr, block, ev.frame, abs, ev.frame * frameLen - 1);
                    MT_CHECK (ev.sampleOffset >= 0 && ev.sampleOffset < len, "sampleOffset %d outside the block of %d", ev.sampleOffset, len);
                }
            }
            const long long expected = static_cast<long long> (sig.size()) / frameLen;
            MT_CHECK (frames == expected && tr.frameCount() == expected, "%.0f Hz / block %d: %lld frames (trace), %lld (counter), expected %lld",
                      sr, block, frames, tr.frameCount(), expected);
            MT_CHECK (meters == static_cast<int> (expected / MonoTracker::METER_EVERY), "%.0f Hz / block %d: %d meters, expected %lld",
                      sr, block, meters, expected / MonoTracker::METER_EVERY);
        }
    }
    return monotest::finish ("test_mono_decimation");
}
