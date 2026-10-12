/* SPDX-License-Identifier: Apache-2.0 */
#include "mybot_device_command.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/* DeviceCommand objects are small and fixed: one pass rejects unknown or repeated keys. */
static bool read_fields(const mybot_json_t *object, const char *const *names, size_t count,
                        unsigned required, const mybot_json_t **fields) {
    if (!object || object->type != MYBOT_JSON_OBJECT) {
        return false;
    }
    memset(fields, 0, count * sizeof(const mybot_json_t *));
    unsigned seen = 0;
    for (const mybot_json_t *item = object->child; item; item = item->next) {
        size_t index = 0;
        while (index < count && (!item->string || strcmp(item->string, names[index]) != 0)) {
            index++;
        }
        if (index == count || (seen & (1u << index))) {
            return false;
        }
        seen |= 1u << index;
        fields[index] = item;
    }
    return (seen & required) == required;
}

static bool is_string(const mybot_json_t *value, const char *expected) {
    const char *text = mybot_json_get_string(value);
    return text && strcmp(text, expected) == 0;
}

static bool read_id(const mybot_json_t *value, char *destination) {
    const char *text = mybot_json_get_string(value);
    if (!text) {
        return false;
    }
    size_t length = strlen(text);
    if (length == 0 || length >= MYBOT_DEVICE_COMMAND_ID_CAPACITY) {
        return false;
    }
    memcpy(destination, text, length + 1);
    return true;
}

static bool read_integer(const mybot_json_t *value, int minimum, int maximum, int *result) {
    if (!value || value->type != MYBOT_JSON_NUMBER || !isfinite(value->valuedouble) ||
        value->valuedouble < minimum || value->valuedouble > maximum ||
        value->valuedouble != (double)value->valueint) {
        return false;
    }
    *result = (int)value->valueint;
    return true;
}

static int read_digits(const char *text, size_t count) {
    int number = 0;
    for (size_t index = 0; index < count; index++) {
        if (text[index] < '0' || text[index] > '9') {
            return -1;
        }
        number = number * 10 + text[index] - '0';
    }
    return number;
}

static bool leap_year(int year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static int leaps_before(int year) {
    year--;
    return year / 4 - year / 100 + year / 400;
}

/* UTC ISO 8601 seconds or milliseconds; no timezone, locale, or HAL clock assumptions. */
static bool read_utc_ms(const mybot_json_t *value, uint64_t *result) {
    static const int month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const char *text = mybot_json_get_string(value);
    if (!text) {
        return false;
    }
    size_t length = strlen(text);
    if ((length != 20 && length != 24) || text[4] != '-' || text[7] != '-' || text[10] != 'T' ||
        text[13] != ':' || text[16] != ':' || text[length - 1] != 'Z') {
        return false;
    }
    int year = read_digits(text, 4);
    int month = read_digits(text + 5, 2);
    int day = read_digits(text + 8, 2);
    int hour = read_digits(text + 11, 2);
    int minute = read_digits(text + 14, 2);
    int second = read_digits(text + 17, 2);
    int milliseconds = length == 24 ? read_digits(text + 20, 3) : 0;
    if (year < 1970 || month < 1 || month > 12 || day < 1 || hour < 0 || hour > 23 || minute < 0 ||
        minute > 59 || second < 0 || second > 59 || milliseconds < 0 ||
        (length == 24 && text[19] != '.') ||
        day > month_days[month - 1] + (month == 2 && leap_year(year) ? 1 : 0)) {
        return false;
    }
    uint64_t days = (uint64_t)((year - 1970) * 365 + leaps_before(year) - leaps_before(1970));
    for (int index = 1; index < month; index++) {
        days += (uint64_t)month_days[index - 1];
    }
    days += (uint64_t)(day - 1) + (month > 2 && leap_year(year) ? 1u : 0u);
    *result =
        (((days * 24 + (uint64_t)hour) * 60 + (uint64_t)minute) * 60 + (uint64_t)second) * 1000 +
        (uint64_t)milliseconds;
    return true;
}

static bool read_action(const mybot_json_t *name, const mybot_json_t *params,
                        mybot_device_command_t *command) {
    const mybot_json_t *field[1];
    if (is_string(name, "audio.set_volume")) {
        static const char *const names[] = {"level"};
        command->type = MYBOT_DEVICE_COMMAND_SET_VOLUME;
        return read_fields(params, names, 1, 1, field) &&
               read_integer(field[0], 0, 100, &command->params.volume);
    }
    if (is_string(name, "audio.adjust_volume")) {
        static const char *const names[] = {"delta"};
        command->type = MYBOT_DEVICE_COMMAND_ADJUST_VOLUME;
        return read_fields(params, names, 1, 1, field) &&
               read_integer(field[0], -100, 100, &command->params.delta);
    }
    if (is_string(name, "audio.set_mute")) {
        static const char *const names[] = {"muted"};
        if (!read_fields(params, names, 1, 1, field) ||
            (field[0]->type != MYBOT_JSON_TRUE && field[0]->type != MYBOT_JSON_FALSE)) {
            return false;
        }
        command->type = MYBOT_DEVICE_COMMAND_SET_MUTE;
        command->params.muted = field[0]->type == MYBOT_JSON_TRUE;
        return true;
    }
    return false;
}

static bool read_emotion(const mybot_json_t *name, const mybot_json_t *params,
                         mybot_device_command_t *command) {
    static const struct {
        const char *name;
        mybot_lcd_indicator_t indicator;
    } emotions[] = {
        {"emotion.happy", MYBOT_LCD_INDICATOR_HAPPY},
        {"emotion.sad", MYBOT_LCD_INDICATOR_SAD},
        {"emotion.angry", MYBOT_LCD_INDICATOR_ANGRY},
        {"emotion.surprised", MYBOT_LCD_INDICATOR_SURPRISED},
    };
    size_t index = 0;
    while (index < sizeof(emotions) / sizeof(emotions[0]) &&
           !is_string(name, emotions[index].name)) {
        index++;
    }
    if (index == sizeof(emotions) / sizeof(emotions[0])) {
        return false;
    }
    /* Current platform presets carry no intensity or duration overrides. */
    if (!params || params->type != MYBOT_JSON_OBJECT || params->child) {
        return false;
    }
    command->type = MYBOT_DEVICE_COMMAND_EMOTION;
    command->params.emotion = emotions[index].indicator;
    return true;
}

bool mybot_device_command_parse(const mybot_json_t *root, mybot_device_command_t *command) {
    static const char *const names[] = {"type",       "schema",     "version",
                                        "message_id", "command_id", "conversation_id",
                                        "issued_at",  "expires_at", "command"};
    const mybot_json_t *fields[9];
    int version;
    if (!command || !read_fields(root, names, 9, (1u << 9) - 1, fields) ||
        !is_string(fields[0], "device_command") ||
        !is_string(fields[1], "botstation.device-command") ||
        !read_integer(fields[2], 1, 1, &version)) {
        return false;
    }
    memset(command, 0, sizeof(*command));
    if (!read_id(fields[3], command->message_id) || !read_id(fields[4], command->command_id) ||
        !read_id(fields[5], command->conversation_id) ||
        !read_utc_ms(fields[6], &command->issued_at_ms) ||
        !read_utc_ms(fields[7], &command->expires_at_ms) ||
        command->issued_at_ms >= command->expires_at_ms) {
        return false;
    }
    static const char *const command_names[] = {"type", "name", "params"};
    const mybot_json_t *command_fields[3];
    if (!read_fields(fields[8], command_names, 3, 7, command_fields)) {
        return false;
    }
    if (is_string(command_fields[0], "action")) {
        return read_action(command_fields[1], command_fields[2], command);
    }
    return is_string(command_fields[0], "scene") &&
           read_emotion(command_fields[1], command_fields[2], command);
}
