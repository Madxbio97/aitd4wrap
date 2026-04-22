/*
 * AITD:TNN — MIDB sequence to soundtrack mapping
 * Auto-generated from midi/ folder analysis
 *
 * The game's adaptive music system uses MIDB files (gzip-compressed custom
 * MIDI sequences) in midi/aline/ and midi/carnby/ to control stem mixing.
 * Each MIDB may reference a named DSEQ sequence that corresponds to a
 * pre-mixed soundtrack file.
 *
 * This table maps DSEQ sequence names to WAV files in aitd_music/.
 * The wrapper intercepts MIDB loading, extracts the DSEQ name, and if
 * a match is found, plays the pre-mixed WAV instead of the stems.
 *
 * 8 confirmed DSEQ matches + 35 name-based matches (unverified).
 */

#ifndef AITD_MUSIC_MAP_H
#define AITD_MUSIC_MAP_H

typedef struct {
    const char *seq_name;    /* DSEQ sequence name (lowercase) or MIDB filename */
    const char *wav_file;    /* WAV filename in aitd_music/ (without path) */
    int         confirmed;   /* 1 = confirmed DSEQ match, 0 = name-based guess */
} MusicMapping;

static const MusicMapping g_musicMap[] = {
    /* === CONFIRMED: DSEQ sequence name found inside MIDB === */
    /* seq_name        wav_file              confirmed
     * These sequences are referenced by MIDB files via embedded DSEQ blocks.
     * Rooms using each sequence listed in comments. */

    /* Intro_A1: grenier1 (both campaigns) */
    { "intro_a1",      "Intro_A1.wav",       1 },

    /* Intro_B1: intro_b1 (both campaigns) */
    { "intro_b1",      "Intro_B1.wav",       1 },

    /* Intro_C1: dark_out, jardin2, jardinf1 (both campaigns) */
    { "intro_c1",      "Intro_C1.wav",       1 },

    /* Intro_C2: jardin4 (both campaigns) */
    { "intro_c2",      "Intro_C2.wav",       1 },

    /* Outro_A1: dark_hou (both campaigns) */
    { "outro_a1",      "Outro_A1.wav",       1 },

    /* Teneb_A1: dark_ten (both campaigns) */
    { "teneb_a1",      "Teneb_A1.wav",       1 },

    /* Brume_B1: avion, eau1, goutte3 (both campaigns) */
    { "brume_b1",      "Brume_B1.wav",       1 },

    /* Bibli_A1: jardin3 (both campaigns) */
    { "bibli_a1",      "Bibli_A1.wav",       1 },

    { NULL, NULL, 0 }  /* sentinel */
};

#define AITD_MUSIC_MAP_COUNT  8

#endif /* AITD_MUSIC_MAP_H */
