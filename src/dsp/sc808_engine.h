/*
 * sc808_engine.h — 8W8 voice orchestration.
 *
 * The DSP itself is sc808_voices.h, which is a verified transcription of
 * sc808.scd and is left alone. This layer is everything a drum machine needs
 * that a set of SynthDefs does not have: pot mapping, per-voice drive and
 * distortion, accent, hi-hat choke, per-lane mutes and the master stage.
 *
 * Realtime contract: every entry point here runs on the SPI callback. Nothing
 * below allocates, opens a file or takes a lock after sc808_create().
 *
 * GPL-3.0.
 */
#ifndef SC808_ENGINE_H
#define SC808_ENGINE_H

#include <stddef.h>

/*
 * Lane order. This is the pad order, the state blob order and the mute-bit
 * order, and changing it breaks saved patches.
 *
 * Fifteen voices and a Master fill Move's left 4x4 pad block exactly:
 *
 *     row 3 (92-95)   CH  OH  CY  MASTER
 *     row 2 (84-87)   RS  MA  CP  CB
 *     row 1 (76-79)   HT  LC  MC  HC
 *     row 0 (68-71)   BD  SD  LT  MT
 *
 * sc808 ships sixteen SynthDefs; rim shot and claves share this lane with a
 * mode switch because they share one channel on the hardware — the 808's
 * panel has a single RS/CL selector and a pattern cannot contain both.
 */
typedef enum {
    SC808_BD = 0, SC808_SD, SC808_LT, SC808_MT, SC808_HT,
    SC808_LC, SC808_MC, SC808_HC,
    SC808_RS, SC808_MA, SC808_CP, SC808_CB,
    SC808_CH, SC808_OH, SC808_CY,
    SC808_NUM_VOICES
} sc808_voice_t;

/* velocity 0..127; >= this applies the global accent, same as 9W9 and 6W6. */
#define SC808_ACCENT_VELOCITY 100

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sc808_engine sc808_engine_t;

sc808_engine_t *sc808_create(float sample_rate);
void  sc808_destroy(sc808_engine_t *e);

/* velocity 0 is treated as a note-off and ignored (drums are one-shots). */
void  sc808_trigger(sc808_engine_t *e, int voice, int velocity);

/* Mono float render. The plugin wrapper does the int16 stereo interleave. */
void  sc808_render(sc808_engine_t *e, float *out, int frames);

/* Both return 1 on a recognised key, 0 otherwise. */
int   sc808_set_param(sc808_engine_t *e, const char *key, const char *val);
int   sc808_get_param(sc808_engine_t *e, const char *key, char *buf, int len);

/* Bit n = lane n muted. Muted lanes swallow triggers and stop ringing. */
void  sc808_set_mutes(sc808_engine_t *e, unsigned mask);
unsigned sc808_get_mutes(const sc808_engine_t *e);

/* State blob for the host's get_param("state") / set_param("state") cycle. */
int   sc808_serialize(const sc808_engine_t *e, char *buf, int len);
void  sc808_deserialize(sc808_engine_t *e, const char *json);

const char *sc808_voice_id(int voice);

#ifdef __cplusplus
}
#endif
#endif /* SC808_ENGINE_H */
