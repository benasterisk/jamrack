// MidPluck: the events both engines (MONO, port of js/audio/guitar/tracker.js;
// POLY, port of js/audio/guitar/poly/engine.js) hand to their caller (the VST3
// shell in plugin/src, the dump_events tool in plugin/tools).
//
// Real-time contract (docs/plugin-plan.md, rule 3.3-1): engines never
// allocate in process(); they append to a caller-owned fixed-capacity list.
#pragma once

#include <cmath>
#include <cstdint>

namespace midpluck
{
    enum class EventType : std::uint8_t
    {
        NoteOn,   // midi, vel, why
        NoteOff,  // midi
        Bend,     // semis (MONO only)
        Meter     // level/pitch/cost readout for the editor (~50 per second)
    };

    struct Event
    {
        EventType type = EventType::Meter;

        // Index of the input sample (in the block passed to process()) at which
        // the decision fell: the sample that completed the analysis frame/hop.
        // For flush() events: 0.
        int sampleOffset = 0;

        // NoteOn / NoteOff: note number AFTER the octave/transpose shift, as the
        // JS emits it; may lie outside 0..127 (the shell clamps, plan 3.2).
        int midi = 0;
        double vel = 0.0;          // NoteOn: 0..1, not quantised
        const char* why = "";      // NoteOn: "pluck" | "legato" | "swell" | "fix" | "poly" (static strings)
        double semis = 0.0;        // Bend: semitones (MONO)

        // POLY: index of the analysis hop that decided the event (-1 for MONO).
        // MONO: index of the analysis frame (k in tracker.js).
        long long frame = -1;

        // Meter (MONO fills db/hz/midiF/conf/note/latMs; POLY fills db/nmfMs/hopMs/voices/eco/gainDb)
        double db = -200.0, hz = 0.0, midiF = NAN, conf = 0.0, latMs = 0.0;
        int note = -1;
        double nmfMs = 0.0, hopMs = 0.0, gainDb = 0.0;
        int voices = 0;
        bool eco = false;
    };

    /** Fixed-capacity event list, owned by the caller, cleared by the caller. */
    struct EventList
    {
        static constexpr int capacity = 1024;
        Event items[capacity];
        int count = 0;
        int dropped = 0;           // events that did not fit (should stay 0: callers feed <= 512-sample chunks)

        void clear() noexcept { count = 0; dropped = 0; }
        void push (const Event& e) noexcept
        {
            if (count < capacity) items[count++] = e;
            else ++dropped;
        }
    };
}
