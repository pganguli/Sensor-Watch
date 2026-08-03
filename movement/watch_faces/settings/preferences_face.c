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

#include <stdlib.h>
#include "preferences_face.h"
#include "watch.h"
#include "watch_utility.h"
// For MOVEMENT_HAS_BUZZER and CLOCK_FACE_24H_ONLY. Included without defining
// MOVEMENT_CONFIG_DEFINE_FACES, so this pulls in the build-time flags only, not
// the watch_faces[] table (which only movement.c may define).
#include "movement_config.h"

#ifndef MOVEMENT_HAS_BUZZER
#define MOVEMENT_HAS_BUZZER 1
#endif

#ifdef CLOCK_FACE_24H_ONLY
#define PREFERENCES_FACE_HAS_CL 0
#else
#define PREFERENCES_FACE_HAS_CL 1
#endif

// Page order: DST, CL (if not 24h-only), CH (if buzzer present), BT (if
// buzzer present), TO, LE, LT duration, LT color x2.
// "Timeout Always" (to_always) is intentionally NOT a page here -- see
// MOVEMENT_DEFAULT_TIMEOUT_ALWAYS in movement_config.h for why it's a
// build-time constant instead of a runtime preference.
#define PREFERENCES_FACE_PAGE_DST         (0)
#define PREFERENCES_FACE_PAGE_CL          (PREFERENCES_FACE_PAGE_DST + 1)
#define PREFERENCES_FACE_PAGE_CH          (PREFERENCES_FACE_PAGE_CL + PREFERENCES_FACE_HAS_CL)
#define PREFERENCES_FACE_PAGE_BT          (PREFERENCES_FACE_PAGE_CH + MOVEMENT_HAS_BUZZER)
#define PREFERENCES_FACE_PAGE_TO          (PREFERENCES_FACE_PAGE_BT + MOVEMENT_HAS_BUZZER)
#define PREFERENCES_FACE_PAGE_LE          (PREFERENCES_FACE_PAGE_TO + 1)
#define PREFERENCES_FACE_PAGE_LT_DURATION (PREFERENCES_FACE_PAGE_LE + 1)
#define PREFERENCES_FACE_PAGE_LT_COLOR1   (PREFERENCES_FACE_PAGE_LT_DURATION + 1)
#define PREFERENCES_FACE_PAGE_LT_COLOR2   (PREFERENCES_FACE_PAGE_LT_COLOR1 + 1)
#define PREFERENCES_FACE_NUM_PREFERENCES  (PREFERENCES_FACE_PAGE_LT_COLOR2 + 1)

const char preferences_face_titles[PREFERENCES_FACE_NUM_PREFERENCES][11] = {
    "DT  DST   ",   // Daylight Saving Time: apply the DST shift to zones that observe it
#if PREFERENCES_FACE_HAS_CL
    "CL        ",   // Clock: 12 or 24 hour
#endif
#if MOVEMENT_HAS_BUZZER
    "CH  Chime ",   // Hourly Chime: on or off
    "BT  Beep  ",   // Buttons: should they beep?
#endif
    "TO        ",   // Timeout: how long before we snap back to the clock face?
    "LE        ",   // Low Energy mode: how long before it engages?
    "LT        ",   // Light: duration
#ifdef WATCH_IS_BLUE_BOARD
    "LT   blu  ",   // Light: blue component (for watches with blue LED)
#else
    "LT   grn  ",   // Light: green component
#endif
    "LT   red  ",   // Light: red component
};

void preferences_face_setup(movement_settings_t *settings, uint8_t watch_face_index, void ** context_ptr) {
    (void) settings;
    (void) watch_face_index;
    if (*context_ptr == NULL) *context_ptr = malloc(sizeof(uint8_t));
}

void preferences_face_activate(movement_settings_t *settings, void *context) {
    (void) settings;
    *((uint8_t *)context) = 0;
    movement_request_tick_frequency(4); // we need to manually blink some pixels
}

bool preferences_face_loop(movement_event_t event, movement_settings_t *settings, void *context) {
    uint8_t current_page = *((uint8_t *)context);
    switch (event.event_type) {
        case EVENT_TICK:
        case EVENT_ACTIVATE:
            // Do nothing; handled below.
            break;
        case EVENT_MODE_BUTTON_UP:
            watch_set_led_off();
            movement_move_to_next_face();
            return false;
        case EVENT_LIGHT_BUTTON_DOWN:
            current_page = (current_page + 1) % PREFERENCES_FACE_NUM_PREFERENCES;
            *((uint8_t *)context) = current_page;
            break;
        case EVENT_ALARM_BUTTON_UP:
            switch (current_page) {
                case PREFERENCES_FACE_PAGE_DST: {
                    // Reinterpret-and-rewrite the RTC the same way marking a new home zone
                    // does in simple_world_face: settings->bit.time_zone is the home zone's
                    // index, so the RTC's own wall-clock is assumed to already be home-zone
                    // local time. Flipping dst_enabled changes that zone's effective offset,
                    // so without this the RTC would silently drift out of sync with the new
                    // interpretation (and simple_world_face's home-zone display, which reads
                    // the RTC directly, would stop matching away-zone math). For UTC (the
                    // power-on default before any home zone is ever marked) the DST delta is
                    // zero, so this is a no-op until a real home zone is set.
                    int16_t old_offset = movement_get_timezone_offset(settings);
                    settings->bit.dst_enabled = !(settings->bit.dst_enabled);
                    int16_t new_offset = movement_get_timezone_offset(settings);
                    if (new_offset != old_offset) {
                        watch_date_time old_local = watch_rtc_get_date_time();
                        uint32_t now_utc = watch_utility_date_time_to_unix_time(old_local, old_offset * 60);
                        watch_date_time new_local = watch_utility_date_time_from_unix_time(now_utc, new_offset * 60);
                        watch_rtc_set_date_time(new_local);
                        // Persist immediately rather than waiting for resign. Unlike every other
                        // preference here, this one has already had a side effect on the RTC; if a
                        // reset landed between the shift and the resign, the clock would stay
                        // shifted while the flag reverted, leaving the watch an hour off with no
                        // visible cause.
                        watch_store_backup_data(settings->reg, 0);
                    }
                    break;
                }
#if PREFERENCES_FACE_HAS_CL
                case PREFERENCES_FACE_PAGE_CL:
                    settings->bit.clock_mode_24h = !(settings->bit.clock_mode_24h);
                    break;
#endif
#if MOVEMENT_HAS_BUZZER
                case PREFERENCES_FACE_PAGE_CH:
                    settings->bit.hourly_chime_enabled = !(settings->bit.hourly_chime_enabled);
                    break;
                case PREFERENCES_FACE_PAGE_BT:
                    settings->bit.button_should_sound = !(settings->bit.button_should_sound);
                    break;
#endif
                case PREFERENCES_FACE_PAGE_TO:
                    settings->bit.to_interval = settings->bit.to_interval + 1;
                    break;
                case PREFERENCES_FACE_PAGE_LE:
                    settings->bit.le_interval = settings->bit.le_interval + 1;
                    break;
                case PREFERENCES_FACE_PAGE_LT_DURATION:
                    settings->bit.led_duration = settings->bit.led_duration + 1;
                    if (settings->bit.led_duration > 3) {
                        settings->bit.led_duration = 0b111;
                    }
                    break;
                case PREFERENCES_FACE_PAGE_LT_COLOR1:
                    settings->bit.led_green_color = settings->bit.led_green_color + 1;
                    break;
                case PREFERENCES_FACE_PAGE_LT_COLOR2:
                    settings->bit.led_red_color = settings->bit.led_red_color + 1;
                    break;
            }
            break;
        case EVENT_ALARM_LONG_PRESS:
            switch (current_page) {
#if PREFERENCES_FACE_HAS_CL
                case PREFERENCES_FACE_PAGE_CL:
                    if (settings->bit.clock_mode_24h)
                        settings->bit.clock_24h_leading_zero = !(settings->bit.clock_24h_leading_zero);
                    break;
#endif
            }
            break;
        case EVENT_TIMEOUT:
            movement_move_to_face(0);
            break;
        default:
            return movement_default_loop_handler(event, settings);
    }
    watch_display_string((char *)preferences_face_titles[current_page], 0);

    // blink active setting on even-numbered quarter-seconds
    if (event.subsecond % 2) {
        char buf[8];
        switch (current_page) {
            case PREFERENCES_FACE_PAGE_DST:
                if (settings->bit.dst_enabled) watch_display_string("y", 9);
                else watch_display_string("n", 9);
                break;
#if PREFERENCES_FACE_HAS_CL
            case PREFERENCES_FACE_PAGE_CL:
                if (settings->bit.clock_mode_24h) {
                    if (settings->bit.clock_24h_leading_zero) watch_display_string("024h", 4);
                    else watch_display_string("24h", 4);
                } else watch_display_string("12h", 4);
                break;
#endif
#if MOVEMENT_HAS_BUZZER
            case PREFERENCES_FACE_PAGE_CH:
                if (settings->bit.hourly_chime_enabled) watch_display_string("y", 9);
                else watch_display_string("n", 9);
                break;
            case PREFERENCES_FACE_PAGE_BT:
                if (settings->bit.button_should_sound) watch_display_string("y", 9);
                else watch_display_string("n", 9);
                break;
#endif
            case PREFERENCES_FACE_PAGE_TO:
                switch (settings->bit.to_interval) {
                    case 0:
                        watch_display_string("60 SeC", 4);
                        break;
                    case 1:
                        watch_display_string("2 n&in", 4);
                        break;
                    case 2:
                        watch_display_string("5 n&in", 4);
                        break;
                    case 3:
                        watch_display_string("30n&in", 4);
                        break;
                }
                break;
            case PREFERENCES_FACE_PAGE_LE:
                switch (settings->bit.le_interval) {
                    case 0:
                        watch_display_string(" Never", 4);
                        break;
                    case 1:
                        watch_display_string("10n&in", 4);
                        break;
                    case 2:
                        watch_display_string("1 hour", 4);
                        break;
                    case 3:
                        watch_display_string("2 hour", 4);
                        break;
                    case 4:
                        watch_display_string("6 hour", 4);
                        break;
                    case 5:
                        watch_display_string("12 hr", 4);
                        break;
                    case 6:
                        watch_display_string(" 1 day", 4);
                        break;
                    case 7:
                        watch_display_string(" 7 day", 4);
                        break;
                }
                break;
            case PREFERENCES_FACE_PAGE_LT_DURATION:
                if (settings->bit.led_duration == 0) {
                    watch_display_string("instnt", 4);
                } else if (settings->bit.led_duration == 0b111) {
                    watch_display_string("no LEd", 4);
                } else {
                    sprintf(buf, " %1d SeC", settings->bit.led_duration * 2 - 1);
                    watch_display_string(buf, 4);
                }
                break;
            case PREFERENCES_FACE_PAGE_LT_COLOR1:
                sprintf(buf, "%2d", settings->bit.led_green_color);
                watch_display_string(buf, 8);
                break;
            case PREFERENCES_FACE_PAGE_LT_COLOR2:
                sprintf(buf, "%2d", settings->bit.led_red_color);
                watch_display_string(buf, 8);
                break;
        }
    }

#if MOVEMENT_HAS_BUZZER
    // the bell indicator reflects the hourly chime setting only while viewing that page.
    if (current_page == PREFERENCES_FACE_PAGE_CH && settings->bit.hourly_chime_enabled) watch_set_indicator(WATCH_INDICATOR_BELL);
    else watch_clear_indicator(WATCH_INDICATOR_BELL);
#endif

    // on LED color select screns, preview the color.
    if (current_page == PREFERENCES_FACE_PAGE_LT_COLOR1 || current_page == PREFERENCES_FACE_PAGE_LT_COLOR2) {
        watch_set_led_color(settings->bit.led_red_color ? (0xF | settings->bit.led_red_color << 4) : 0,
                            settings->bit.led_green_color ? (0xF | settings->bit.led_green_color << 4) : 0);
        // return false so the watch stays awake (needed for the PWM driver to function).
        return false;
    }

    watch_set_led_off();
    return true;
}

void preferences_face_resign(movement_settings_t *settings, void *context) {
    (void) settings;
    (void) context;
    watch_set_led_off();
    watch_store_backup_data(settings->reg, 0);
}
