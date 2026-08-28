/*
 * sc808_shape.h — the post-voice drive stage.
 *
 * SEVEN characters, the same seven 9W9 and 6W6 offer, so a player who knows
 * what BFZ does on the 909 knows what it does here. The CURVES are 9W9's,
 * copied rather than re-derived — they were tuned and approved by ear there,
 * and going looking for other references was explicitly not wanted.
 *
 * WHAT IS NOT 9W9's, AND WHY
 *
 * The drive CONTRACT is 8W8's own, and it is deliberately not the one 9W9
 * and 6W6 ship. Their pot is (0.85, 12) EXP, where the bottom of the knob is
 * transparent only by crossfade and unity sits a few ticks up. Ours is
 * LINEAR 0..10 defaulting to 0, and 0 is a BIT-EXACT bypass — the sample is
 * not shaped at all. That came out of the same field report 9W9's range
 * change came out of ("0 to 55 nothing happens and then it crackles") and it
 * answers it more directly: the 808 had no drive stage, so a fresh patch has
 * none, and the knob adds saturation from its first tick.
 *
 * So each curve below is 9W9's shape driven by k = 1 + drive — which is what
 * the Clip and Fold curves here already did — divided by a MAKEUP of k to
 * some power, so the stage adds saturation and not level. Diode is the one
 * exception and needs no exponent: tanh normalises exactly at half scale in
 * closed form.
 *
 * The exponents are MEASURED, not guessed — each is the value that holds a
 * decaying 180 Hz drum's loudness flattest across the whole throw, and
 * tools/fx_probe re-measures them and fails if any curve moves the kit by
 * more than 6 dB. Every one of these curves has a small-signal gain that
 * rises with k, so a shaper left un-made-up lifts the TAIL of a drum rather
 * than its body: PDIST measured +9.1 dB that way and BFZ +11.5.
 *
 * That is what keeps the knob from being a loudness control that walks the
 * master sum into the wrapper's clip, which is the fault the old
 * tanh(kx)/tanh(k) normalisation shipped: it pinned the PEAK at unity and
 * handed the small signal up to +18 dB.
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

/*
 * Crush decimates as well as quantises, so it carries state: the held sample
 * and the decimator's phase. Every lane has its own pair and so does the
 * master stage — sharing one would make the hats' sample-and-hold audible on
 * the kick.
 */
#define SC808_CRUSH_STATE 2

static inline float sc808_diode_round(const float _x, const float _k)
{
    if(_k < SC808_DRIVE_BYPASS) return _x;
    /* Normalised at x = 0.5: tanh(k*0.5) * 0.5/tanh(0.5k) == 0.5. */
    return tanhf(_k * _x) * (0.5f / tanhf(0.5f * _k));
}

/* The SAT / BFZ / PDIST cores, exactly 9W9's algebra, kept as their own
 * functions so the makeup exponents below are visibly the only thing this
 * module adds to them. */
static inline float sc808_sat_core(const float _x, const float _k)
{
    const float u   = _x * _k + 0.08f * _k * _x * _x;
    const float wet = u / (1.0f + fabsf(u));
    return 0.35f * _x + 0.8775f * wet;
}

static inline float sc808_bfz_core(const float _x, const float _k)
{
    /* The 2.5x pre-gain fuzzes even at the bottom of the knob, so the fuzz
     * is crossfaded in over the first of the range — 9W9's own guard, and it
     * agrees with this module's "the knob starts from dry" rule. */
    const float g   = _k * 2.5f;
    const float u   = _x * g + 0.22f;
    /* 9W9 writes this offset as the rounded 0.18033. Spelled exactly it is
     * 0.22/1.22 — the curve's own value at silence — and rounding it leaves
     * the stage sitting on a DC step of about -114 dBFS under the whole kit.
     * Inaudible, free to remove, and tools/fx_probe asks for silence in,
     * silence out. */
    const float wet = (u / (1.0f + fabsf(u)) - 0.22f / 1.22f) * 1.05f;
    float m = (_k - 0.85f) / 1.15f;
    if(m < 0.0f) m = 0.0f;
    if(m > 1.0f) m = 1.0f;
    return (1.0f - m) * _x + m * wet;
}

static inline float sc808_pdist_core(const float _x, const float _k)
{
    float u = _x * _k + 0.12f;
    if(u >  1.0f) u =  1.0f;
    if(u < -1.0f) u = -1.0f;
    const float y0 = 0.12f - (0.12f * 0.12f * 0.12f) / 3.0f;   /* silence -> 0 */
    return ((u - u * u * u / 3.0f) - y0) * (1.5f / 1.479f);
}

/*
 * 0 Diode, 1 Clip, 2 SAT, 3 BFZ, 4 PDIST, 5 Fold, 6 Crush.
 *
 * MENU ORDER IS STORAGE ORDER and it changed when SAT/BFZ/PDIST arrived:
 * Fold moved 2 -> 5 and Crush 3 -> 6. sc808_deserialize remaps blobs written
 * before that. Never reorder again without extending the migration.
 *
 * `_st` is SC808_CRUSH_STATE floats, or null for a stateless call — only
 * Crush reads it, and without it Crush quantises but does not decimate.
 */
static inline float sc808_shape_st(const float _x, const float _drive,
                                   const int _type, float *_st)
{
    if(_drive < SC808_DRIVE_BYPASS) return _x;
    /* k = 1 + drive: k = 1 is the transparent point every curve below is
     * written around, and the knob climbs from there. */
    const float k = 1.0f + _drive;
    switch(_type)
    {
    case 1: {   /* hard clip — aggressive, square-ish */
        float v = _x * k;
        if(v >  1.0f) v =  1.0f;
        if(v < -1.0f) v = -1.0f;
        /* partial makeup: full 1/k would cancel the loudness a clip is
         * bought for, none at all is the old crackle. */
        return v / powf(k, 0.6f);
    }
    case 2:     /* SAT — warm, parallel, keeps the transient */
        return sc808_sat_core(_x, k)   / powf(k, 0.42f);
    case 3:     /* BFZ — thick fuzz wall */
        return sc808_bfz_core(_x, k)   / powf(k, 0.65f);
    case 4:     /* PDIST — biased cubic crunch */
        return sc808_pdist_core(_x, k) / powf(k, 0.45f);
    case 5: {   /* wavefolder — metallic, odd harmonics rise with drive */
        float v = _x * k;
        for(int i = 0; i < 3; ++i)
        {
            if(v >  1.0f) v =  2.0f - v;
            if(v < -1.0f) v = -2.0f - v;
        }
        return v / powf(k, 0.5f);
    }
    case 6: {   /* bitcrush — quantise AND decimate, lo-fi grit */
        /* Depth falls and the hold stretches together as the knob rises:
         * from (transparent, full rate) to (~3 levels, a few kHz). */
        const float steps = 1.5f + 9.0f / k;
        float q = floorf(_x * steps + 0.5f) / steps;
        if(_st)
        {
            const float hold = k < 1.0f ? 1.0f : 1.0f + (k - 1.0f) * 1.7f;
            _st[1] += 1.0f;                       /* decimator phase */
            if(_st[1] >= hold) { _st[1] -= hold; _st[0] = q; }
            q = _st[0];
        }
        return q;
    }
    case 0:
    default:
        return sc808_diode_round(_x, _drive);
    }
}

/* Stateless call, for the checks and tools that have no per-voice state.
 * Crush quantises without decimating through this door. */
static inline float sc808_shape(const float _x, const float _drive, const int _type)
{
    return sc808_shape_st(_x, _drive, _type, 0);
}

#endif /* SC808_SHAPE_H */
