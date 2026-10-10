// Port of js/audio/guitar/poly/profile.js: the measured guitar profile behind
// the generic POLY template bank. The numbers live in the generated
// src/poly/profile_data.h (plugin/tools/gen-profile-header.mjs).
#pragma once

namespace midpluck::poly
{
    constexpr int PROFILE_PARTIALS = 40;

    /** A guitar profile, as buildBank() consumes it (profile.js / a calibrated profile). */
    struct Profile
    {
        double bLaw[6][2];                  // { B_open_fit, slope_per_6_frets } per string
        double prof[6][PROFILE_PARTIALS];   // partial amplitudes a_n, n = 1..40, per string
    };

    /** { bLaw: B_LAW, prof: PROF_ATT } of profile.js (the generic bank). */
    const Profile& genericProfile() noexcept;

    /** PROF_DEC of profile.js (decay-phase profile, unused by the engine). */
    const double (&profileDecay() noexcept)[6][PROFILE_PARTIALS];
}
