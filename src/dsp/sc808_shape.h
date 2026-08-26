/*
 * sc808_shape.h — the post-voice drive stage.
 *
 * REWRITTEN after field testing, and deliberately no longer the maths 9W9 and
 * 6W6 ship. The old stage was tanh(k*x)/tanh(k) with k straight off the pot,
 * and that normalisation is a trap: it pins the PEAK at unity and hands the
 * small-signal range a gain of k/tanh(k) — up to +18 dB of clean-sounding
 * loudness that then slams the master sum into the wrapper's hard clip. What
 * the player reported is exactly what it was: "0 to 55 nothing happens"
 * (below unity input a gentle tanh barely bends) "and then it crackles" (the
 * boost clipping downstream, not the diode shaping anything).
 *
 * The contract now:
 *
 *   - Drive 0 is a BIT-EXACT bypass. Not "nearly linear" — the sample goes
 *     through untouched, and it is the default. The 808 had no drive stage;
 *     a fresh patch should not have one either.
 *
 *   - The knob adds SATURATION, not level. Each curve is normalised at half
 *     scale, where the trims put a voice's body: a signal at 0.5 keeps its
 *     peak, everything above compresses, and the tail below lifts gently the
 *     way any compressed drum's tail does. Net loudness stays close enough
 *     to flat that the master sum no longer walks into the clip.
 *
 *   - The effect starts moving from the first few pot ticks, because k is
 *     LINEAR in the pot. The old exponential map spent half its throw between
 *     0.2 and 1.0 where a tanh has nothing to say.
 *
 * `_drive` arrives as the pot's engineering value, 0..10, from gen_params.
 *
 * GPL-3.0.
 */
#ifndef SC808_SHAPE_H
#define SC808_SHAPE_H

#include <math.h>

/* Below this the stage is a wire. Explicit, so "0 means off" is a promise
 * about the code and not about float behaviour. */
#define SC808_DRIVE_BYPASS 1.0e-3f

static inline float sc808_diode_round(const float _x, const float _k)
{
    if(_k < SC808_DRIVE_BYPASS) return _x;
    /* Normalised at x = 0.5: tanh(k*0.5) * 0.5/tanh(0.5k) == 0.5. */
    return tanhf(_k * _x) * (0.5f / tanhf(0.5f * _k));
}

/* 0 diode, 1 hard clip, 2 wavefolder, 3 bitcrush. */
static inline float sc808_shape(const float _x, const float _drive, const int _type)
{
    if(_drive < SC808_DRIVE_BYPASS) return _x;
    switch(_type)
    {
    case 1: {   /* hard clip — aggressive, square-ish */
        const float g = 1.0f + _drive;
        float v = _x * g;
        if(v >  1.0f) v =  1.0f;
        if(v < -1.0f) v = -1.0f;
        /* partial makeup: full 1/g would cancel the loudness a clip is
         * bought for, none at all is the old crackle. */
        return v / powf(g, 0.6f);
    }
    case 2: {   /* wavefolder — metallic, odd harmonics rise with drive */
        const float g = 1.0f + _drive;
        float v = _x * g;
        for(int i = 0; i < 3; ++i)
        {
            if(v >  1.0f) v =  2.0f - v;
            if(v < -1.0f) v = -2.0f - v;
        }
        return v / powf(g, 0.5f);
    }
    case 3: {   /* bitcrush — lo-fi grit; steps fall as the knob rises */
        const float steps = 2.0f + 240.0f / _drive;
        return floorf(_x * steps + 0.5f) / steps;
    }
    case 0:
    default:
        return sc808_diode_round(_x, _drive);
    }
}

#endif /* SC808_SHAPE_H */
