// The notes MidPluck holds in the host (docs/plugin-plan.md, contract 3.2):
// the C++ twin of the `sounding` Set of js/input/guitar.js. Fixed storage, no
// allocation (audio thread). Iteration follows insertion order, as a JS Set
// does, so "release everything" sends its note-offs in the order the JS
// would. Each note remembers the MIDI channel its note-on went out on, so
// that its note-off reaches the same channel even if MIDI CH changed since.
#pragma once

#include <cstdint>

namespace midpluck
{
    class Sounding
    {
    public:
        static constexpr int numNotes = 128;

        bool has (int midi) const noexcept { return valid (midi) && channel_[midi] != 0; }
        bool empty() const noexcept { return count_ == 0; }
        int size() const noexcept { return count_; }

        /** Set.add: a note already held keeps its place in the order (its channel is updated). */
        void add (int midi, int channel) noexcept
        {
            if (! valid (midi) || channel < 1 || channel > 16) return;
            if (channel_[midi] == 0) order_[count_++] = static_cast<std::uint8_t> (midi);
            channel_[midi] = static_cast<std::uint8_t> (channel);
        }

        /** Set.delete: returns the channel the note was held on, 0 if it was not held. */
        int remove (int midi) noexcept
        {
            if (! has (midi)) return 0;
            const int channel = channel_[midi];
            channel_[midi] = 0;
            for (int i = 0; i < count_; ++i)
            {
                if (order_[i] != midi) continue;
                for (int j = i + 1; j < count_; ++j) order_[j - 1] = order_[j];
                --count_;
                break;
            }
            return channel;
        }

        /** Calls fn(midi, channel) for every held note in insertion order, then empties the set. */
        template <class Fn>
        void releaseAll (Fn&& fn) noexcept
        {
            for (int i = 0; i < count_; ++i)
            {
                const int midi = order_[i];
                fn (midi, static_cast<int> (channel_[midi]));
                channel_[midi] = 0;
            }
            count_ = 0;
        }

        void clear() noexcept
        {
            for (int i = 0; i < count_; ++i) channel_[order_[i]] = 0;
            count_ = 0;
        }

    private:
        static bool valid (int midi) noexcept { return midi >= 0 && midi < numNotes; }

        std::uint8_t channel_[numNotes] = {};   // 0 = not held, else 1..16
        std::uint8_t order_[numNotes] = {};
        int count_ = 0;
    };
}
