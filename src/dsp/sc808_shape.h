/*
 * sc808_shape.h — the post-voice drive stage.
 *
 * Identical maths to 9W9's er99_circuit.h and 6W6's sd606_shape.h, on purpose:
 * the three kits should respond the same way to the same knob, so a player who
 * knows what Fold at 90 does on the 606 knows what it does here.
 *
 * The 808 voices have their own internal nonlinearities — the bass drum's
 * op-amp clip, the cymbal's swing-VCA diodes — and those live in the voices
 * where the circuit puts them. This file is the panel's Drive/Distortion,
 * which the hardware never had.
 *
 * GPL-3.0.
 */
#ifndef SC808_SHAPE_H
#define SC808_SHAPE_H

#include <math.h>

/*
 * Back-to-back diode rounding. Sharp peaks are what make a raw oscillator
 * sound buzzy; the diodes conduct near the peaks and round them off. A tanh
 * soft-clip is the standard model of that pair, normalised so unity drive
 * leaves the level alone.
 */
static inline float sc808_diode_round(const float _x, const float _drive)
{
    const float k = _drive > 0.01f ? _drive : 0.01f;
    return tanhf(k * _x) / tanhf(k);
}

/* 0 diode, 1 hard clip, 2 wavefolder, 3 bitcrush. */
static inline float sc808_shape(const float _x, const float _drive, const int _type)
{
    const float k = _drive > 0.01f ? _drive : 0.01f;
    switch(_type)
    {
    case 1: {   /* hard clip — aggressive, square-ish */
        float v = _x * k;
        if(v >  1.0f) v =  1.0f;
        if(v < -1.0f) v = -1.0f;
        return v;
    }
    case 2: {   /* wavefolder — metallic, odd harmonics rise with drive */
        float v = _x * k;
        for(int i = 0; i < 3; ++i)
        {
            if(v >  1.0f) v =  2.0f - v;
            if(v < -1.0f) v = -2.0f - v;
        }
        return v;
    }
    case 3: {   /* bitcrush / decimate — lo-fi grit */
        const float steps = 2.0f + 30.0f / k;
        return floorf(_x * steps + 0.5f) / steps;
    }
    case 0:
    default:
        return sc808_diode_round(_x, k);
    }
}

#endif /* SC808_SHAPE_H */
