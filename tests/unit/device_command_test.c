/* SPDX-License-Identifier: Apache-2.0 */
#include "mybot_device_command.h"

#include <api/aosl.h>

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define VALID_ACTION "{\"type\":\"action\",\"name\":\"audio.set_volume\",\"params\":{\"level\":50}}"
static mybot_json_t *fixture(const char *command, const char *extra) {
    char text[2048];
    int length = snprintf(text, sizeof(text),
                          "{\"type\":\"device_command\",\"schema\":\"botstation.device-command\","
                          "\"version\":1,\"message_id\":\"msg-1\",\"command_id\":\"cmd-1\","
                          "\"conversation_id\":\"conv-1\","
                          "\"command\":%s%s}",
                          command, extra);
    assert(length > 0 && (size_t)length < sizeof(text));
    mybot_json_t *root = mybot_json_parse(text);
    assert(root != NULL);
    return root;
}

static bool parse(const char *command, mybot_device_command_t *result) {
    mybot_json_t *root = fixture(command, "");
    bool valid = mybot_device_command_parse(root, result);
    mybot_json_delete(root);
    return valid;
}

static void test_valid_commands(void) {
    mybot_device_command_t command;
    assert(parse(VALID_ACTION, &command));
    assert(command.type == MYBOT_DEVICE_COMMAND_SET_VOLUME && command.params.volume == 50);
    assert(strcmp(command.message_id, "msg-1") == 0);
    assert(strcmp(command.command_id, "cmd-1") == 0);
    assert(strcmp(command.conversation_id, "conv-1") == 0);

    assert(
        parse("{\"type\":\"action\",\"name\":\"audio.adjust_volume\",\"params\":{\"delta\":-10}}",
              &command));
    assert(command.type == MYBOT_DEVICE_COMMAND_ADJUST_VOLUME && command.params.delta == -10);
    assert(parse("{\"type\":\"action\",\"name\":\"audio.set_mute\",\"params\":{\"muted\":true}}",
                 &command));
    assert(command.type == MYBOT_DEVICE_COMMAND_SET_MUTE && command.params.muted);
    assert(parse("{\"type\":\"action\",\"name\":\"audio.set_mute\",\"params\":{\"muted\":false}}",
                 &command));
    assert(!command.params.muted);

    static const struct {
        const char *name;
        mybot_lcd_indicator_t indicator;
    } scenes[] = {
        {"happy", MYBOT_LCD_INDICATOR_HAPPY},
        {"sad", MYBOT_LCD_INDICATOR_SAD},
        {"angry", MYBOT_LCD_INDICATOR_ANGRY},
        {"surprised", MYBOT_LCD_INDICATOR_SURPRISED},
    };
    for (size_t index = 0; index < sizeof(scenes) / sizeof(scenes[0]); index++) {
        char text[128];
        int length = snprintf(text, sizeof(text),
                              "{\"type\":\"scene\",\"name\":\"emotion.%s\",\"params\":{}}",
                              scenes[index].name);
        assert(length > 0 && (size_t)length < sizeof(text));
        assert(parse(text, &command));
        assert(command.type == MYBOT_DEVICE_COMMAND_EMOTION);
        assert(command.params.emotion == scenes[index].indicator);
    }
}

static void test_parameter_boundaries(void) {
    static const struct {
        const char *name;
        const char *key;
        const char *value;
        bool valid;
    } cases[] = {
        {"audio.set_volume", "level", "0", true},
        {"audio.set_volume", "level", "100", true},
        {"audio.set_volume", "level", "-1", false},
        {"audio.set_volume", "level", "101", false},
        {"audio.set_volume", "level", "0.5", false},
        {"audio.set_volume", "level", "99.9", false},
        {"audio.set_volume", "level", "\"50\"", false},
        {"audio.set_volume", "level", "true", false},
        {"audio.adjust_volume", "delta", "-100", true},
        {"audio.adjust_volume", "delta", "100", true},
        {"audio.adjust_volume", "delta", "-101", false},
        {"audio.adjust_volume", "delta", "101", false},
        {"audio.adjust_volume", "delta", "-0.5", false},
        {"audio.adjust_volume", "delta", "null", false},
        {"audio.set_mute", "muted", "true", true},
        {"audio.set_mute", "muted", "false", true},
        {"audio.set_mute", "muted", "1", false},
        {"audio.set_mute", "muted", "\"true\"", false},
    };
    mybot_device_command_t result;
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
        char command[192];
        int length = snprintf(command, sizeof(command),
                              "{\"type\":\"action\",\"name\":\"%s\",\"params\":{\"%s\":%s}}",
                              cases[index].name, cases[index].key, cases[index].value);
        assert(length > 0 && (size_t)length < sizeof(command));
        assert(parse(command, &result) == cases[index].valid);
    }
}

static void test_invalid_commands(void) {
    static const char *const cases[] = {
        "{}",
        "[]",
        "{\"type\":\"action\",\"name\":\"audio.unknown\",\"params\":{}}",
        "{\"type\":\"ACTION\",\"name\":\"audio.set_volume\",\"params\":{\"level\":50}}",
        "{\"type\":\"action\",\"name\":\"Audio.set_volume\",\"params\":{\"level\":50}}",
        "{\"type\":\"action\",\"name\":\"audio.set_volume\",\"params\":{}}",
        "{\"type\":\"action\",\"name\":\"audio.set_volume\",\"params\":[]} ",
        "{\"type\":\"action\",\"name\":\"audio.set_volume\",\"params\":null}",
        "{\"type\":\"action\",\"name\":\"audio.set_volume\",\"params\":{\"Level\":50}}",
        "{\"type\":\"action\",\"name\":\"audio.set_volume\",\"params\":{\"level\":50,\"extra\":0}}",
        "{\"type\":\"action\",\"name\":\"audio.set_volume\",\"params\":{\"level\":1,\"level\":2}}",
        ("{\"type\":\"action\",\"type\":\"action\",\"name\":\"audio.set_volume\",\"params\":{"
         "\"level\":50}}"),
        "{\"type\":\"action\",\"name\":\"audio.set_volume\",\"params\":{\"level\":50},\"extra\":0}",
        "{\"type\":\"scene\",\"name\":\"emotion.annoyed\",\"params\":{}}",
        "{\"type\":\"scene\",\"name\":\"emotion.happy\",\"params\":[]}",
        "{\"type\":\"scene\",\"name\":\"emotion.happy\",\"params\":{\"intensity\":0.8}}",
        "{\"type\":\"scene\",\"name\":\"emotion.happy\",\"params\":{\"duration_ms\":5000}}",
        "{\"type\":\"scene\",\"name\":\"audio.set_volume\",\"params\":{}}",
        "{\"type\":\"action\",\"name\":\"emotion.happy\",\"params\":{}}",
    };
    mybot_device_command_t result;
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
        assert(!parse(cases[index], &result));
    }
}

static void test_envelope(void) {
    mybot_device_command_t result;
    assert(!mybot_device_command_parse(NULL, &result));
    assert(!mybot_device_command_parse(NULL, NULL));
    mybot_json_t *root = fixture(VALID_ACTION, "");
    assert(!mybot_device_command_parse(root, NULL));
    mybot_json_delete(root);

    static const char *const extras[] = {",\"version\":1", ",\"extra\":0", ",\"Version\":1"};
    for (size_t index = 0; index < sizeof(extras) / sizeof(extras[0]); index++) {
        root = fixture(VALID_ACTION, extras[index]);
        assert(!mybot_device_command_parse(root, &result));
        mybot_json_delete(root);
    }
    root = mybot_json_parse("{\"type\":\"device_command\"}");
    assert(root != NULL && !mybot_device_command_parse(root, &result));
    mybot_json_delete(root);
    root = mybot_json_parse("[]");
    assert(root != NULL && !mybot_device_command_parse(root, &result));
    mybot_json_delete(root);

    static const char *const required[] = {"type",       "schema",          "version", "message_id",
                                           "command_id", "conversation_id", "command"};
    for (size_t index = 0; index < sizeof(required) / sizeof(required[0]); index++) {
        root = fixture(VALID_ACTION, "");
        mybot_json_t **link = &root->child;
        while (*link && strcmp((*link)->string, required[index]) != 0) {
            link = &(*link)->next;
        }
        assert(*link != NULL);
        mybot_json_t *field = *link;
        *link = field->next;
        assert(!mybot_device_command_parse(root, &result));
        *link = field;
        mybot_json_delete(root);
    }

    static const char *const strings[] = {"type", "schema", "message_id", "command_id",
                                          "conversation_id"};
    for (size_t index = 0; index < sizeof(strings) / sizeof(strings[0]); index++) {
        root = fixture(VALID_ACTION, "");
        mybot_json_t *field = mybot_json_get_object_item(root, strings[index]);
        assert(field != NULL);
        field->type = MYBOT_JSON_NULL;
        assert(!mybot_device_command_parse(root, &result));
        field->type = MYBOT_JSON_STRING;
        field->valuestring[0] = '\0';
        assert(!mybot_device_command_parse(root, &result));
        mybot_json_delete(root);
    }
    static const double versions[] = {0, 2, 1.5, NAN, INFINITY};
    for (size_t index = 0; index < sizeof(versions) / sizeof(versions[0]); index++) {
        root = fixture(VALID_ACTION, "");
        mybot_json_t *version = mybot_json_get_object_item(root, "version");
        version->valuedouble = versions[index];
        assert(!mybot_device_command_parse(root, &result));
        version->type = MYBOT_JSON_STRING;
        assert(!mybot_device_command_parse(root, &result));
        mybot_json_delete(root);
    }
}

static void test_id_bounds(void) {
    mybot_device_command_t result;
    static const char *const names[] = {"message_id", "command_id", "conversation_id"};
    char id[MYBOT_DEVICE_COMMAND_ID_CAPACITY + 1];
    memset(id, 'x', sizeof(id) - 1);
    id[sizeof(id) - 1] = '\0';
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
        for (size_t length = MYBOT_DEVICE_COMMAND_ID_CAPACITY - 1;
             length <= MYBOT_DEVICE_COMMAND_ID_CAPACITY; length++) {
            id[length] = '\0';
            mybot_json_t *root = fixture(VALID_ACTION, "");
            mybot_json_t *field = mybot_json_get_object_item(root, names[index]);
            char *original = field->valuestring;
            field->valuestring = id;
            assert(mybot_device_command_parse(root, &result) ==
                   (length < MYBOT_DEVICE_COMMAND_ID_CAPACITY));
            field->valuestring = original;
            mybot_json_delete(root);
            id[length] = 'x';
        }
    }
}

static void test_ignored_timestamps(void) {
    static const char *const timestamps[] = {
        "",
        ",\"issued_at\":\"1970-01-01T00:00:00Z\",\"expires_at\":\"1970-01-01T00:00:01Z\"",
        ",\"issued_at\":\"9999-12-31T23:59:58Z\",\"expires_at\":\"9999-12-31T23:59:59Z\"",
        ",\"issued_at\":\"2026-09-21T10:00:10Z\",\"expires_at\":\"2026-09-21T10:00:00Z\"",
        ",\"issued_at\":\"not a date\",\"expires_at\":\"\"",
        ",\"issued_at\":null",
        ",\"expires_at\":false",
        ",\"issued_at\":123,\"expires_at\":{}",
        ",\"issued_at\":[],\"expires_at\":true",
    };
    mybot_device_command_t command;
    for (size_t index = 0; index < sizeof(timestamps) / sizeof(timestamps[0]); index++) {
        mybot_json_t *root = fixture(VALID_ACTION, timestamps[index]);
        assert(mybot_device_command_parse(root, &command));
        assert(command.type == MYBOT_DEVICE_COMMAND_SET_VOLUME && command.params.volume == 50);
        mybot_json_delete(root);
    }
    static const char *const repeated[] = {
        ",\"issued_at\":null,\"issued_at\":null",
        ",\"expires_at\":null,\"expires_at\":null",
    };
    for (size_t index = 0; index < sizeof(repeated) / sizeof(repeated[0]); index++) {
        mybot_json_t *root = fixture(VALID_ACTION, repeated[index]);
        assert(!mybot_device_command_parse(root, &command));
        mybot_json_delete(root);
    }
}

int main(void) {
    aosl_ctor();
    test_valid_commands();
    test_parameter_boundaries();
    test_invalid_commands();
    test_envelope();
    test_id_bounds();
    test_ignored_timestamps();
    aosl_dtor();
    puts("device_command_test: ok");
    return 0;
}
