/* SPDX-License-Identifier: MIT */

/*
 * MIT License
 *
 * Copyright © 2021-2022 Joey Castillo <joeycastillo@utexas.edu> <jose.castillo@gmail.com>
 * Copyright © 2022 Alexsander Akers <me@a2.io>
 * Copyright © 2022 TheOnePerson <a.nebinger@web.de>
 * Copyright © 2023 Alex Utter <ooterness@gmail.com>
 * Copyright © 2023 Konrad Rieck
 * Copyright © 2024 Matheus Afonso Martins Moreira <matheus.a.m.moreira@gmail.com>
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

#ifndef SIMPLE_WORLD_FACE_H_
#define SIMPLE_WORLD_FACE_H_

/*
 * SIMPLE WORLD FACE
 *
 * Displays the current local time, just like the original watch, but also lets
 * you cycle through a curated list of other timezones. Merges clock_face's
 * weekday/battery display with world_clock2_face's multi-zone browsing.
 *
 * Display mode:
 *  * On your designated home zone: shows weekday, day, and time; checks and
 *    indicates low battery (LAP indicator), same as the plain clock face.
 *    The BELL indicator reflects the global "hourly chime" preference
 *    (movement_settings_t.bit.hourly_chime_enabled), which is configured
 *    from the preferences face, not from this face.
 *  * On any other selected zone: shows that zone's two-letter abbreviation
 *    instead of the weekday, and skips the battery check (redundant away
 *    from home).
 *  * ALARM short press: cycle forward to the next selected zone (wraps
 *    around). There is no "previous zone" gesture in display mode, so that
 *    LIGHT can stay a plain backlight button, with none of its presses
 *    doubling as zone navigation.
 *  * LIGHT short or long press: backlight (default behavior), and also
 *    immediately shows the ISO week number in place of seconds for a couple
 *    of ticks before reverting back to the normal seconds display. Both
 *    press lengths trigger the same flash -- the short/long distinction
 *    exists for other faces' backlight-duration semantics, not for this one
 *    -- and the redraw happens right away rather than waiting for the next
 *    natural tick, so the flash tracks the press instead of appearing on a
 *    delay of up to a second.
 *  * ALARM long press: enter zone-settings mode.
 *
 * Zone-settings mode (also entered automatically on first-ever boot):
 *  * ALARM short press: move forward through all 39 zones.
 *  * LIGHT short press: move backward through all 39 zones (backlight is
 *    suppressed here so it doesn't fire on every navigation press).
 *  * LIGHT long press: toggle selection of the current zone.
 *  * MODE short press: confirm and return to display mode (does not affect
 *    face navigation elsewhere; display mode's MODE button still moves to
 *    the next face as normal).
 *  * ALARM long press: mark the current zone (must be selected) as home.
 *    This is deliberately a long press, not a short one, so it can't be
 *    triggered by a reflexive tap while browsing. This is the "I've
 *    landed in a new zone" gesture: it reinterprets the RTC's current
 *    wall-clock reading under the OLD movement_settings_t.bit.time_zone
 *    offset to recover the absolute instant, then rewrites the RTC to
 *    that same instant's wall-clock time under the NEW zone's offset, and
 *    updates bit.time_zone to match. In other words, marking a new home
 *    zone shifts the displayed time for you -- you should not need to
 *    manually redial the hour/minute on set_time_face after traveling;
 *    that face is only for correcting drift or initial setup. set_time_face
 *    no longer exposes a ZO page at all, so this is the only place
 *    bit.time_zone is ever changed -- no risk of the two drifting apart.
 *    Exception: the very first time you ever mark a home zone, the RTC is
 *    NOT shifted -- there's no previous home zone to use as a trustworthy
 *    "old" offset yet, so shifting would corrupt whatever local time you
 *    just dialed in on set_time_face. That first marking only labels the
 *    current wall-clock as belonging to the chosen zone.
 *  * MODE long press: unbound in this mode; falls through to the default
 *    behavior (jump to the first/secondary face).
 *
 * In zone-settings mode, SIGNAL indicates the currently browsed zone is
 * selected, and BELL indicates it's the home zone -- both indicators mean
 * something different here than they do in display mode.
 *
 * Selected zones, the current zone, and the home zone persist across power
 * loss using two of the watch's RTC backup registers.
 */

#include "movement.h"
// For SIMPLE_WORLD_FACE_SNAP_ON_CHIME. Included without defining
// MOVEMENT_CONFIG_DEFINE_FACES, so this pulls in the build-time flags only, not
// the watch_faces[] table (which only movement.c may define).
#include "movement_config.h"

// Derived from the framework's table rather than restated, so this can't drift out of
// sync with movement_timezone_offsets. NUM_TIME_ZONES is a name shared with
// world_clock2_face.h; both now resolve to the same value, so the redefinition is benign.
#ifndef NUM_TIME_ZONES
#define NUM_TIME_ZONES MOVEMENT_NUM_TIME_ZONES
#endif

// Zone-settings mode ignores movement's shared to_always/EVENT_TIMEOUT machinery:
// since this face is normally installed at index 0, the framework's forced
// "go to face 0 on timeout" is a no-op here (you're already on face 0), and
// with to_always on, EVENT_TIMEOUT is never even delivered to any face. So
// this face tracks its own idle ticks in settings mode and self-exits back to
// display mode. Ticks run at 4Hz while in settings mode (see *_activate).
#ifndef SIMPLE_WORLD_FACE_SETTINGS_IDLE_TICKS
#define SIMPLE_WORLD_FACE_SETTINGS_IDLE_TICKS (60 * 4)
#endif

// Set to 1 to jump the display back to this face whenever the hourly chime sounds.
// The chime is delivered as a background task, so it fires from whatever face is in
// the foreground; with this off (the default) the chime sounds without disturbing
// what you were looking at. Requires the chime to be enabled at all, which in turn
// requires MOVEMENT_HAS_BUZZER.
#ifndef SIMPLE_WORLD_FACE_SNAP_ON_CHIME
#define SIMPLE_WORLD_FACE_SNAP_ON_CHIME 0
#endif

typedef enum {
    SIMPLE_WORLD_FACE_MODE_DISPLAY,
    SIMPLE_WORLD_FACE_MODE_SETTINGS
} simple_world_face_mode_t;

typedef struct {
    bool selected;
} simple_world_face_zone_t;

typedef struct {
    uint32_t previous_date_time;
    uint8_t last_battery_check;
    uint8_t watch_face_index;
    bool battery_low;
    bool refresh_face;
    uint8_t week_flash_ticks;
    uint16_t settings_idle_ticks;

    simple_world_face_mode_t current_mode;
    simple_world_face_zone_t zones[NUM_TIME_ZONES];
    uint8_t current_zone;
    uint8_t home_zone;
    bool has_home_zone;

    uint8_t backup_register_a;
    uint8_t backup_register_b;
} simple_world_state_t;

void simple_world_face_setup(movement_settings_t *settings, uint8_t watch_face_index, void ** context_ptr);
void simple_world_face_activate(movement_settings_t *settings, void *context);
bool simple_world_face_loop(movement_event_t event, movement_settings_t *settings, void *context);
void simple_world_face_resign(movement_settings_t *settings, void *context);
bool simple_world_face_wants_background_task(movement_settings_t *settings, void *context);

#define simple_world_face ((const watch_face_t) { \
    simple_world_face_setup, \
    simple_world_face_activate, \
    simple_world_face_loop, \
    simple_world_face_resign, \
    simple_world_face_wants_background_task, \
})

#endif // SIMPLE_WORLD_FACE_H_
