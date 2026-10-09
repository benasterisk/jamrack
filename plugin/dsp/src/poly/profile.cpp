// Port of js/audio/guitar/poly/profile.js (see midpluck/poly/profile.h).

#include "midpluck/poly/profile.h"
#include "profile_data.h"

namespace midpluck::poly
{
    namespace
    {
        Profile makeGeneric() noexcept
        {
            Profile p {};
            for (int s = 0; s < 6; s++)
            {
                p.bLaw[s][0] = generated::B_LAW[s][0];
                p.bLaw[s][1] = generated::B_LAW[s][1];
                for (int n = 0; n < PROFILE_PARTIALS; n++) p.prof[s][n] = generated::PROF_ATT[s][n];
            }
            return p;
        }
    }

    const Profile& genericProfile() noexcept
    {
        static const Profile p = makeGeneric();
        return p;
    }

    const double (&profileDecay() noexcept)[6][PROFILE_PARTIALS]
    {
        return generated::PROF_DEC;
    }
}
