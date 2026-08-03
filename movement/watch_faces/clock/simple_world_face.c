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

#include <stdlib.h>
#include <string.h>
#include "simple_world_face.h"
#include "watch.h"
#include "watch_utility.h"
#include "watch_private_display.h"

#ifndef SIMPLE_WORLD_FACE_LOW_BATTERY_VOLTAGE_THRESHOLD
#define SIMPLE_WORLD_FACE_LOW_BATTERY_VOLTAGE_THRESHOLD 2200
#endif

#ifndef SIMPLE_WORLD_FACE_WEEK_FLASH_TICKS
#define SIMPLE_WORLD_FACE_WEEK_FLASH_TICKS 2
#endif

/* Bit layout of the second backup register (the first register holds a
 * plain 32-bit selection bitmask for zones 0-31). */
#define SIMPLE_WORLD_FACE_REG_B_ZONES_32_38_SHIFT  0  // 7 bits: selection for zones 32-38
#define SIMPLE_WORLD_FACE_REG_B_HOME_ZONE_SHIFT    7  // 6 bits: home zone index
#define SIMPLE_WORLD_FACE_REG_B_CURRENT_ZONE_SHIFT 13 // 6 bits: current zone index
#define SIMPLE_WORLD_FACE_REG_B_HAS_HOME_BIT       19 // 1 bit: home zone is set
#define SIMPLE_WORLD_FACE_REG_B_MAGIC_SHIFT        24 // 8 bits: format magic, see below

/* Magic/version stamp for the saved format. Backup registers survive a reflash,
 * so a build with a different zone table would otherwise silently inherit the
 * previous build's saved indices -- which now mean different zones, and may be
 * out of range entirely (the table shrank from 41 to 39 entries when the two
 * fixed "daylight" pseudo-zones were removed). An out-of-range index would index
 * past zones[]/zone_names[]/movement_timezone_offsets[]. Bump this whenever the
 * zone table or this bit layout changes, so stale data is discarded instead of
 * misread. The old layout never wrote bits 24-31, so it can't collide. */
#define SIMPLE_WORLD_FACE_REG_B_MAGIC              0xA1

static inline unsigned int simple_world_face_mod(int a, int b) {
    int r = a % b;
    return r < 0 ? r + b : r;
}

/* Bounded on purpose: a plain "walk until we come back to where we started"
 * loop never terminates if current_zone is somehow out of range, since the
 * walk is taken modulo NUM_TIME_ZONES and would never revisit it. */
static uint8_t simple_world_face_find_selected_zone(simple_world_state_t *state, int direction) {
    uint8_t i = simple_world_face_mod(state->current_zone, NUM_TIME_ZONES);
    for (int n = 0; n < NUM_TIME_ZONES; n++) {
        i = simple_world_face_mod(i + direction, NUM_TIME_ZONES);
        if (state->zones[i].selected) return i;
    }
    // no zone is selected at all.
    return state->has_home_zone ? state->home_zone : 0;
}

static void simple_world_face_load_state(simple_world_state_t *state) {
    memset(state->zones, 0, sizeof(state->zones));
    state->current_zone = 0;
    state->home_zone = 0;
    state->has_home_zone = false;
    state->current_mode = SIMPLE_WORLD_FACE_MODE_SETTINGS;

    if (!state->backup_register_a || !state->backup_register_b) return;

    uint32_t data_a = watch_get_backup_data(state->backup_register_a);
    uint32_t data_b = watch_get_backup_data(state->backup_register_b);

    // Reject anything not written by this exact format (see the magic's comment).
    if (((data_b >> SIMPLE_WORLD_FACE_REG_B_MAGIC_SHIFT) & 0xFF) != SIMPLE_WORLD_FACE_REG_B_MAGIC) return;

    uint8_t home_zone = (data_b >> SIMPLE_WORLD_FACE_REG_B_HOME_ZONE_SHIFT) & 0x3F;
    uint8_t current_zone = (data_b >> SIMPLE_WORLD_FACE_REG_B_CURRENT_ZONE_SHIFT) & 0x3F;
    // Belt and braces: the magic should already rule this out, but an index past
    // the end of the zone table would corrupt memory rather than just look wrong.
    if (home_zone >= NUM_TIME_ZONES || current_zone >= NUM_TIME_ZONES) return;

    for (int i = 0; i < 32; i++) {
        state->zones[i].selected = (data_a >> i) & 1;
    }
    for (int i = 0; i < NUM_TIME_ZONES - 32; i++) {
        state->zones[32 + i].selected = (data_b >> (SIMPLE_WORLD_FACE_REG_B_ZONES_32_38_SHIFT + i)) & 1;
    }

    state->home_zone = home_zone;
    state->current_zone = current_zone;
    state->has_home_zone = (data_b >> SIMPLE_WORLD_FACE_REG_B_HAS_HOME_BIT) & 1;
    state->current_mode = SIMPLE_WORLD_FACE_MODE_DISPLAY;
}

static void simple_world_face_save_state(simple_world_state_t *state) {
    if (!state->backup_register_a || !state->backup_register_b) return;

    uint32_t data_a = 0;
    for (int i = 0; i < 32; i++) {
        if (state->zones[i].selected) data_a |= (1UL << i);
    }

    uint32_t data_b = 0;
    for (int i = 0; i < NUM_TIME_ZONES - 32; i++) {
        if (state->zones[32 + i].selected) data_b |= (1UL << (SIMPLE_WORLD_FACE_REG_B_ZONES_32_38_SHIFT + i));
    }
    data_b |= ((uint32_t)(state->home_zone & 0x3F)) << SIMPLE_WORLD_FACE_REG_B_HOME_ZONE_SHIFT;
    data_b |= ((uint32_t)(state->current_zone & 0x3F)) << SIMPLE_WORLD_FACE_REG_B_CURRENT_ZONE_SHIFT;
    if (state->has_home_zone) data_b |= (1UL << SIMPLE_WORLD_FACE_REG_B_HAS_HOME_BIT);
    data_b |= ((uint32_t)SIMPLE_WORLD_FACE_REG_B_MAGIC) << SIMPLE_WORLD_FACE_REG_B_MAGIC_SHIFT;

    watch_store_backup_data(data_a, state->backup_register_a);
    watch_store_backup_data(data_b, state->backup_register_b);
}

/* Beep when zone is enabled. An octave up */
static void simple_world_face_beep_enable(void) {
    watch_buzzer_play_note(BUZZER_NOTE_G7, 50);
    watch_buzzer_play_note(BUZZER_NOTE_REST, 75);
    watch_buzzer_play_note(BUZZER_NOTE_C8, 75);
}

/* Beep when zone is disabled. An octave down */
static void simple_world_face_beep_disable(void) {
    watch_buzzer_play_note(BUZZER_NOTE_C8, 50);
    watch_buzzer_play_note(BUZZER_NOTE_REST, 75);
    watch_buzzer_play_note(BUZZER_NOTE_G7, 75);
}

void simple_world_face_setup(movement_settings_t *settings, uint8_t watch_face_index, void ** context_ptr) {
    (void) settings;

    if (*context_ptr == NULL) {
        *context_ptr = malloc(sizeof(simple_world_state_t));
        memset(*context_ptr, 0, sizeof(simple_world_state_t));
        simple_world_state_t *state = (simple_world_state_t *) *context_ptr;
        state->watch_face_index = watch_face_index;
        state->backup_register_a = movement_claim_backup_register();
        state->backup_register_b = movement_claim_backup_register();
        simple_world_face_load_state(state);
    }
}

void simple_world_face_activate(movement_settings_t *settings, void *context) {
    (void) settings;
    simple_world_state_t *state = (simple_world_state_t *) context;

    if (watch_tick_animation_is_running()) watch_stop_tick_animation();

    movement_request_tick_frequency(state->current_mode == SIMPLE_WORLD_FACE_MODE_SETTINGS ? 4 : 1);
    state->refresh_face = true;
}

/* Snap back to the home zone. Away-zone browsing in display mode is meant to be a
 * quick peek, not a new resting state, so both EVENT_TIMEOUT and entering low energy
 * mode end it automatically. A no-op if there's no home zone yet or we're home already,
 * so it's safe to call on every LE tick without re-writing the backup registers. */
static void simple_world_face_return_home(simple_world_state_t *state) {
    if (!state->has_home_zone || state->current_zone == state->home_zone) return;
    state->current_zone = state->home_zone;
    state->previous_date_time = 0xFFFFFFFF;
    simple_world_face_save_state(state);
}

/* The actual redraw for display mode. Pulled out of the event switch so a button
 * handler can force an immediate redraw (see EVENT_LIGHT_BUTTON_UP below) instead of
 * only updating on the next natural EVENT_TICK, which could be up to a second away. */
static void simple_world_face_render_display(movement_event_t event, movement_settings_t *settings, simple_world_state_t *state) {
    char buf[16];
    uint8_t pos;
    watch_date_time date_time;
    uint32_t previous_date_time;
    uint32_t timestamp;

    // Browsing an away zone is meant to be a peek, not a new default: once we're
    // idle enough to enter low energy mode, snap back to home rather than leaving
    // the low-power display parked on wherever we were looking.
    if (event.event_type == EVENT_LOW_ENERGY_UPDATE) simple_world_face_return_home(state);

    if (state->refresh_face) {
        watch_clear_indicator(WATCH_INDICATOR_LAP);
        watch_set_colon();
        if (settings->bit.hourly_chime_enabled) watch_set_indicator(WATCH_INDICATOR_BELL);
        else watch_clear_indicator(WATCH_INDICATOR_BELL);
        if (settings->bit.alarm_enabled) watch_set_indicator(WATCH_INDICATOR_SIGNAL);
        else watch_clear_indicator(WATCH_INDICATOR_SIGNAL);
        movement_update_24h_indicator(settings);
        state->previous_date_time = 0xFFFFFFFF;
        state->refresh_face = false;
    }

    bool is_home = state->has_home_zone && (state->current_zone == state->home_zone);

    date_time = watch_rtc_get_date_time();
    if (!is_home) {
        timestamp = watch_utility_date_time_to_unix_time(date_time, movement_get_timezone_offset_for_zone(settings->bit.time_zone, settings) * 60);
        date_time = watch_utility_date_time_from_unix_time(timestamp, movement_get_timezone_offset_for_zone(state->current_zone, settings) * 60);
    }

    if (is_home) {
        if (date_time.unit.day != state->last_battery_check) {
            state->last_battery_check = date_time.unit.day;
            watch_enable_adc();
            uint16_t voltage = watch_get_vcc_voltage();
            watch_disable_adc();
            state->battery_low = (voltage < SIMPLE_WORLD_FACE_LOW_BATTERY_VOLTAGE_THRESHOLD);
        }
        if (state->battery_low) watch_set_indicator(WATCH_INDICATOR_LAP);
        else watch_clear_indicator(WATCH_INDICATOR_LAP);
    } else {
        watch_clear_indicator(WATCH_INDICATOR_LAP);
    }

    previous_date_time = state->previous_date_time;
    state->previous_date_time = date_time.reg;

    bool show_week_flash = state->week_flash_ticks > 0;
    if (event.event_type == EVENT_TICK && show_week_flash) state->week_flash_ticks--;

    bool set_leading_zero = false;
    if (!show_week_flash && (date_time.reg >> 6) == (previous_date_time >> 6) && event.event_type != EVENT_LOW_ENERGY_UPDATE) {
        // everything before seconds is the same, don't waste cycles setting those segments.
        watch_display_character_lp_seconds('0' + date_time.unit.second / 10, 8);
        watch_display_character_lp_seconds('0' + date_time.unit.second % 10, 9);
        return;
    } else if (!show_week_flash && (date_time.reg >> 12) == (previous_date_time >> 12) && event.event_type != EVENT_LOW_ENERGY_UPDATE) {
        // everything before minutes is the same.
        pos = 6;
        snprintf(buf, sizeof(buf), "%02d%02d", date_time.unit.minute, date_time.unit.second);
    } else {
        // other stuff changed (or we're flashing the week number); let's do it all.
        if (!settings->bit.clock_mode_24h) {
            // if we are in 12 hour mode, do some cleanup.
            if (date_time.unit.hour < 12) watch_clear_indicator(WATCH_INDICATOR_PM);
            else watch_set_indicator(WATCH_INDICATOR_PM);
            date_time.unit.hour %= 12;
            if (date_time.unit.hour == 0) date_time.unit.hour = 12;
        } else if (settings->bit.clock_24h_leading_zero && date_time.unit.hour < 10) {
            set_leading_zero = true;
        }

        const char *prefix = is_home ? watch_utility_get_weekday(date_time) : movement_timezone_names[state->current_zone];

        pos = 0;
        if (show_week_flash) {
            snprintf(buf, sizeof(buf), "%.2s%2d%2d%02d%02d", prefix, date_time.unit.day, date_time.unit.hour, date_time.unit.minute,
                    watch_utility_get_weeknumber(date_time.unit.year, date_time.unit.month, date_time.unit.day));
        } else if (event.event_type == EVENT_LOW_ENERGY_UPDATE) {
            if (!watch_tick_animation_is_running()) watch_start_tick_animation(500);
            snprintf(buf, sizeof(buf), "%.2s%2d%2d%02d  ", prefix, date_time.unit.day, date_time.unit.hour, date_time.unit.minute);
        } else {
            snprintf(buf, sizeof(buf), "%.2s%2d%2d%02d%02d", prefix, date_time.unit.day, date_time.unit.hour, date_time.unit.minute, date_time.unit.second);
        }
    }
    watch_display_string(buf, pos);
    if (set_leading_zero) watch_display_string("0", 4);
}

static bool simple_world_face_mode_display(movement_event_t event, movement_settings_t *settings, simple_world_state_t *state) {
    switch (event.event_type) {
        case EVENT_ACTIVATE:
        case EVENT_TICK:
        case EVENT_LOW_ENERGY_UPDATE:
            simple_world_face_render_display(event, settings, state);
            break;
        case EVENT_ALARM_BUTTON_UP:
            state->current_zone = simple_world_face_find_selected_zone(state, +1);
            state->previous_date_time = 0xFFFFFFFF;
            simple_world_face_save_state(state);
            break;
        case EVENT_LIGHT_BUTTON_UP:
        case EVENT_LIGHT_LONG_PRESS:
            // Treated the same regardless of how long LIGHT was held: the short/long
            // distinction exists for other faces' backlight-duration semantics, not for
            // this flash. Rendering immediately (rather than waiting for the next
            // EVENT_TICK, up to a second away) is what makes the flash actually track
            // the button press instead of appearing on a delay.
            state->week_flash_ticks = SIMPLE_WORLD_FACE_WEEK_FLASH_TICKS;
            simple_world_face_render_display(event, settings, state);
            return movement_default_loop_handler(event, settings);
        case EVENT_ALARM_LONG_PRESS:
            state->current_mode = SIMPLE_WORLD_FACE_MODE_SETTINGS;
            state->settings_idle_ticks = 0;
            state->refresh_face = true;
            movement_request_tick_frequency(4);
            if (settings->bit.button_should_sound) watch_buzzer_play_note(BUZZER_NOTE_C8, 50);
            break;
        case EVENT_BACKGROUND_TASK:
#if SIMPLE_WORLD_FACE_SNAP_ON_CHIME
            movement_move_to_face(state->watch_face_index);
#endif
            movement_play_signal();
            break;
        case EVENT_TIMEOUT:
            simple_world_face_return_home(state);
            break;
        default:
            return movement_default_loop_handler(event, settings);
    }

    return true;
}

/* Leave zone-settings mode for display mode, landing on a zone that's actually
 * selected. Shared by every exit path (MODE press, our own idle timeout, and
 * EVENT_TIMEOUT) so they can't drift apart. */
static void simple_world_face_exit_settings(simple_world_state_t *state) {
    if (!state->zones[state->current_zone].selected)
        state->current_zone = simple_world_face_find_selected_zone(state, +1);

    state->current_mode = SIMPLE_WORLD_FACE_MODE_DISPLAY;
    state->refresh_face = true;
    movement_request_tick_frequency(1);
    simple_world_face_save_state(state);
}

/* True for the events that mean "the wearer is still here", which hold off the
 * idle timeout. Kept as one predicate so a newly handled button can't forget to
 * reset the counter -- an omission that has already caused one bug here. */
static bool simple_world_face_event_is_interaction(uint8_t event_type) {
    switch (event_type) {
        case EVENT_ACTIVATE:
        case EVENT_LIGHT_BUTTON_DOWN:
        case EVENT_LIGHT_BUTTON_UP:
        case EVENT_LIGHT_LONG_PRESS:
        case EVENT_ALARM_BUTTON_DOWN:
        case EVENT_ALARM_BUTTON_UP:
        case EVENT_ALARM_LONG_PRESS:
        case EVENT_MODE_BUTTON_DOWN:
        case EVENT_MODE_BUTTON_UP:
        case EVENT_MODE_LONG_PRESS:
            return true;
        default:
            return false;
    }
}

static bool simple_world_face_mode_settings(movement_event_t event, movement_settings_t *settings, simple_world_state_t *state) {
    char buf[16];
    int8_t hours, minutes;
    div_t result;

    if (simple_world_face_event_is_interaction(event.event_type)) state->settings_idle_ticks = 0;

    switch (event.event_type) {
        case EVENT_ACTIVATE:
        case EVENT_TICK:
        case EVENT_LOW_ENERGY_UPDATE:
            if (state->refresh_face) {
                watch_clear_colon();
                watch_clear_indicator(WATCH_INDICATOR_24H);
                watch_clear_indicator(WATCH_INDICATOR_PM);
                watch_clear_indicator(WATCH_INDICATOR_LAP);
                state->refresh_face = false;
            }

            if (event.event_type == EVENT_TICK) {
                state->settings_idle_ticks++;
                if (state->settings_idle_ticks >= SIMPLE_WORLD_FACE_SETTINGS_IDLE_TICKS) {
                    /* Same as confirming, but silent: don't leave the watch stuck mid-configuration. */
                    simple_world_face_exit_settings(state);
                    return true;
                }
            }

            result = div(movement_get_timezone_offset_for_zone(state->current_zone, settings), 60);
            hours = result.quot;
            minutes = result.rem;

            /*
             * Display time zone. The range of the parameters is reduced
             * to avoid accidentally overflowing the buffer and to suppress
             * corresponding compiler warnings.
             */
            snprintf(buf, sizeof(buf), "%.2s%2d %c%02d%02d",
                    movement_timezone_names[state->current_zone],
                    state->current_zone % 100,
                    hours < 0 ? '-' : '+',
                    abs(hours) % 24,
                    abs(minutes) % 60);

            /* Let the zone number blink */
            if (event.subsecond % 2) buf[2] = buf[3] = ' ';

            if (state->zones[state->current_zone].selected)
                watch_set_indicator(WATCH_INDICATOR_SIGNAL);
            else
                watch_clear_indicator(WATCH_INDICATOR_SIGNAL);

            // the bell indicates that the currently displayed zone is the home zone.
            if (state->has_home_zone && state->current_zone == state->home_zone)
                watch_set_indicator(WATCH_INDICATOR_BELL);
            else
                watch_clear_indicator(WATCH_INDICATOR_BELL);

            watch_display_string(buf, 0);
            break;
        case EVENT_ALARM_BUTTON_UP:
            state->current_zone = simple_world_face_mod(state->current_zone + 1, NUM_TIME_ZONES);
            break;
        case EVENT_LIGHT_BUTTON_UP:
            state->current_zone = simple_world_face_mod(state->current_zone - 1, NUM_TIME_ZONES);
            break;
        case EVENT_LIGHT_BUTTON_DOWN:
            /* Do nothing; suppress the default backlight while browsing zones. */
            break;
        case EVENT_LIGHT_LONG_PRESS:
            /* Toggle selection of current zone */
            state->zones[state->current_zone].selected = !state->zones[state->current_zone].selected;

            if (!state->zones[state->current_zone].selected &&
                state->has_home_zone && state->current_zone == state->home_zone) {
                // deselecting the home zone clears home status.
                state->has_home_zone = false;
            }

            if (settings->bit.button_should_sound) {
                if (state->zones[state->current_zone].selected) simple_world_face_beep_enable();
                else simple_world_face_beep_disable();
            }
            simple_world_face_save_state(state);
            break;
        case EVENT_ALARM_LONG_PRESS:
            /* Mark the current zone (if selected) as home. */
            if (state->zones[state->current_zone].selected) {
                // Shift the RTC's own wall-clock to the new zone before relabeling it as
                // home, so the same absolute instant is preserved: reinterpret the current
                // reading under the OLD zone offset to get UTC, then re-render that instant
                // under the NEW zone offset and write it back. This is what lets you just
                // say "I'm in a new zone now" instead of re-entering the local time by hand.
                //
                // Only do this if a home zone was already established: on the very first
                // ever marking, bit.time_zone is still its power-on default (UTC) and does
                // NOT describe whatever local time you just dialed in via set_time_face, so
                // treating it as the "old" zone would corrupt the time you just set. The
                // first marking should just label the current wall-clock, not shift it.
                if (state->has_home_zone) {
                    watch_date_time old_local = watch_rtc_get_date_time();
                    uint32_t now_utc = watch_utility_date_time_to_unix_time(old_local, movement_get_timezone_offset_for_zone(settings->bit.time_zone, settings) * 60);
                    watch_date_time new_local = watch_utility_date_time_from_unix_time(now_utc, movement_get_timezone_offset_for_zone(state->current_zone, settings) * 60);
                    watch_rtc_set_date_time(new_local);
                }

                state->home_zone = state->current_zone;
                state->has_home_zone = true;
                // keep the RTC's own zone reference (used by other faces, and by us for
                // away-zone math) in sync with whichever zone we're now calling home.
                settings->bit.time_zone = state->current_zone;
                if (settings->bit.button_should_sound) watch_buzzer_play_note(BUZZER_NOTE_C8, 50);
                simple_world_face_save_state(state);
            }
            break;
        case EVENT_MODE_BUTTON_UP:
            /* Confirm and return to display mode. */
            simple_world_face_exit_settings(state);
            if (settings->bit.button_should_sound) watch_buzzer_play_note(BUZZER_NOTE_C8, 50);
            break;
        case EVENT_TIMEOUT:
            /* Same as confirming, but silent: don't leave the watch stuck mid-configuration. */
            simple_world_face_exit_settings(state);
            break;
        default:
            return movement_default_loop_handler(event, settings);
    }

    return true;
}

bool simple_world_face_loop(movement_event_t event, movement_settings_t *settings, void *context) {
    simple_world_state_t *state = (simple_world_state_t *) context;
    switch (state->current_mode) {
        case SIMPLE_WORLD_FACE_MODE_DISPLAY:
            return simple_world_face_mode_display(event, settings, state);
        case SIMPLE_WORLD_FACE_MODE_SETTINGS:
            return simple_world_face_mode_settings(event, settings, state);
    }
    return false;
}

void simple_world_face_resign(movement_settings_t *settings, void *context) {
    (void) context;
    // persist settings->bit.time_zone in case marking a home zone changed it.
    watch_store_backup_data(settings->reg, 0);
}

bool simple_world_face_wants_background_task(movement_settings_t *settings, void *context) {
    (void) context;
    if (!settings->bit.hourly_chime_enabled) return false;

    watch_date_time date_time = watch_rtc_get_date_time();

    return date_time.unit.minute == 0;
}
