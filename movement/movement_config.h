/*
 * MIT License
 *
 * Copyright (c) 2022 Joey Castillo
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef MOVEMENT_CONFIG_H_
#define MOVEMENT_CONFIG_H_

/* This header is included both by movement.c, which needs the face table below, and by
 * individual watch faces that just want to see one of the build-time flags further down
 * (MOVEMENT_HAS_BUZZER, CLOCK_FACE_24H_ONLY, etc.) -- those flags are meaningless unless a
 * face's own .c file can see them, and no face was including this file at all until now, so
 * every one of those flags was silently reverting to its in-file fallback default regardless
 * of what's set here. The face table itself, though, is a real global with storage (not just
 * a declaration), so it can only be compiled once: only the one file that actually needs it
 * -- movement.c -- should #define MOVEMENT_CONFIG_DEFINE_FACES before including this header.
 * Any face that just wants the flags should include it without defining that. */
#ifdef MOVEMENT_CONFIG_DEFINE_FACES
#include "movement_faces.h"

const watch_face_t watch_faces[] = {
    simple_world_face,
    stock_stopwatch_face,
    sunrise_sunset_face,
    moon_phase_face,
    probability_face,
    preferences_face,
    set_time_hackwatch_face,
    thermistor_readout_face,
    voltage_face
};

#define MOVEMENT_NUM_FACES (sizeof(watch_faces) / sizeof(watch_face_t))

/* Determines what face to go to from the first face on long press of the Mode button.
 * Also excludes these faces from the normal rotation.
 * In the default firmware, this lets you access temperature and battery voltage with a long press of Mode.
 * Some folks also like to use this to hide the preferences and time set faces from the normal rotation.
 * If you don't want any faces to be excluded, set this to 0 and a long Mode press will have no effect.
 */
#define MOVEMENT_SECONDARY_FACE_INDEX (MOVEMENT_NUM_FACES - 4) // or (0)
#endif // MOVEMENT_CONFIG_DEFINE_FACES

/* Custom hourly chime tune. Check movement_custom_signal_tunes.h for options. */
#define SIGNAL_TUNE_DEFAULT

/* Determines the intensity of the led colors
 * Set a hex value 0-15 with 0x0 being off and 0xF being max intensity
 */
#define MOVEMENT_DEFAULT_GREEN_COLOR 0x0
#define MOVEMENT_DEFAULT_RED_COLOR 0x1

/* Set to true for 24h mode or false for 12h mode */
#define MOVEMENT_DEFAULT_24H_MODE true

/* Force 24h-only mode at build time. Removes the CL page from preferences_face, and
 * suppresses the 24H indicator on simple_world_face (with the mode fixed, a permanently
 * lit indicator conveys nothing). Note that the upstream clock faces interpret this the
 * other way and light the indicator unconditionally, so this only matches on the faces
 * that check for it. */
#define CLOCK_FACE_24H_ONLY

/* Enable or disable the sound on mode button press */
#define MOVEMENT_DEFAULT_BUTTON_SOUND false

/* Set to 0 if you have no buzzer soldered. Gates any preference page in the
 * preferences face that only makes sense with a buzzer present -- currently
 * the button-beep (BT) and hourly chime (CH) toggles -- so they don't even
 * exist to configure. When CH is enabled, simple_world_face reads
 * movement_settings_t.bit.hourly_chime_enabled to decide whether to sound
 * the hourly signal. */
#define MOVEMENT_HAS_BUZZER 0

/* Set to 1 to snap the display back to simple_world_face whenever the hourly chime
 * sounds. The chime is a background task, so it fires from whatever face you happen
 * to be on; off means it sounds without interrupting you. Only has an effect if the
 * chime can be enabled at all, i.e. MOVEMENT_HAS_BUZZER above is 1. */
#define SIMPLE_WORLD_FACE_SNAP_ON_CHIME 0

/* Set the timeout before switching back to the main watch face
 * Valid values are:
 * 0: 60 seconds
 * 1: 2 minutes
 * 2: 5 minutes
 * 3: 30 minutes
 */
#define MOVEMENT_DEFAULT_TIMEOUT_INTERVAL 0

/* Build-time only (no preferences page): whether to unconditionally return to
 * face 0 after the timeout above, regardless of the active face's own
 * EVENT_TIMEOUT handling. Deliberately kept false: when true, the movement
 * dispatcher skips delivering EVENT_TIMEOUT to the active face entirely and
 * force-navigates instead, which bypasses stock_stopwatch_face's own guard
 * against snapping away while a stopwatch is running (and similar guards on
 * any other face). Leaving this false restores normal per-face EVENT_TIMEOUT
 * handling, which every face in this build already implements correctly on
 * its own. */
#define MOVEMENT_DEFAULT_TIMEOUT_ALWAYS false

/* Set the timeout before switching to low energy mode
 * Valid values are:
 * 0: Never
 * 1: 10 minutes
 * 2: 1 hour
 * 3: 2 hours
 * 4: 6 hours
 * 5: 12 hours
 * 6: 1 day
 * 7: 7 days
 */
#define MOVEMENT_DEFAULT_LOW_ENERGY_INTERVAL 1

/* Set the led duration
 * Valid values are:
 * 0: No LED
 * 1: 1 second
 * 2: 3 seconds
 * 3: 5 seconds
 */
#define MOVEMENT_DEFAULT_LED_DURATION 1

#endif // MOVEMENT_CONFIG_H_
