/*
 * w8_engine.h — 8W8 voice orchestration.
 *
 * The DSP itself is w8_voices.h, which is a verified transcription of
 * sc808.scd and is left alone. This layer is everything a drum machine needs
 * that a set of SynthDefs does not have: pot mapping, per-voice drive and
 * distortion, velocity, hi-hat choke, per-lane mutes and the master stage.
 *
 * Realtime contract: every entry point here runs on the SPI callback. Nothing
 * below allocates, opens a file or takes a lock after w8_create().
 *
 * GPL-3.0.
 */
#ifndef W8_ENGINE_H
#define W8_ENGINE_H

#include <stddef.h>

/*
 * Lane order. This is the pad order, the state blob order and the mute-bit
 * order, and changing it breaks saved patches.
 *
 * SIXTEEN voices fill Move's left 4x4 pad block exactly:
 *
 *     row 3 (92-95)   CB  CH  OH  CY
 *     row 2 (84-87)   RS  CL  MA  CP
 *     row 1 (76-79)   HT  LC  MC  HC
 *     row 0 (68-71)   BD  SD  LT  MT
 *
 * The rim shot and the claves have a pad each. They share one CHANNEL on the
 * hardware — the 808's panel has a single RS/CL selector and a pattern cannot
 * contain both — and 8W8 ran them as one lane with a mode switch for exactly
 * that reason. Splitting them is a deliberate departure: Engine A ships them as
 * two SynthDefs, a pattern that wants both is a normal thing to want, and it
 * makes the roster sixteen, which is what the pad block holds.
 *
 * The cost is the Master pad. With sixteen drums there is no sixteenth pad
 * left for it, so Master is reached by the jog — click for the section list,
 * or Shift+Jog — as every other page is. 9W9 and 6W6 keep pad 16 for Master;
 * this one cannot.
 */
typedef enum {
    W8_BD = 0, W8_SD, W8_LT, W8_MT,
    W8_HT, W8_LC, W8_MC, W8_HC,
    W8_RS, W8_CL, W8_MA, W8_CP,
    W8_CB, W8_CH, W8_OH, W8_CY,
    W8_NUM_VOICES
} w8_voice_t;

/* Velocity has no threshold: level follows it all the way up, reaching the
 * full-velocity gain at 127. See the comment in w8_trigger — on this kit
 * it reaches most voices as a trigger VOLTAGE rather than a gain. */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct w8_engine w8_engine_t;

w8_engine_t *w8_create(float sample_rate);
void  w8_destroy(w8_engine_t *e);

/* velocity 0 is treated as a note-off and ignored (drums are one-shots). */
void  w8_trigger(w8_engine_t *e, int voice, int velocity);

/* Mono float render. The plugin wrapper does the int16 stereo interleave. */
void  w8_render(w8_engine_t *e, float *out, int frames);

/* Both return 1 on a recognised key, 0 otherwise. */
int   w8_set_param(w8_engine_t *e, const char *key, const char *val);
int   w8_get_param(w8_engine_t *e, const char *key, char *buf, int len);

/* Bit n = lane n muted. Muted lanes swallow triggers and stop ringing. */
void  w8_set_mutes(w8_engine_t *e, unsigned mask);
unsigned w8_get_mutes(const w8_engine_t *e);

/* State blob for the host's get_param("state") / set_param("state") cycle. */
int   w8_serialize(const w8_engine_t *e, char *buf, int len);
void  w8_deserialize(w8_engine_t *e, const char *json);

const char *w8_voice_id(int voice);

#ifdef __cplusplus
}
#endif
#endif /* W8_ENGINE_H */
