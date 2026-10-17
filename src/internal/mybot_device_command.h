/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_DEVICE_COMMAND_H_
#define MYBOT_DEVICE_COMMAND_H_

#include "mybot_json.h"

#include <stdbool.h>

#define MYBOT_DEVICE_COMMAND_ID_CAPACITY 128

typedef struct {
    char message_id[MYBOT_DEVICE_COMMAND_ID_CAPACITY];
    char command_id[MYBOT_DEVICE_COMMAND_ID_CAPACITY];
    char conversation_id[MYBOT_DEVICE_COMMAND_ID_CAPACITY];
    int volume;
} mybot_device_command_t;

/* Parse the inner DeviceCommand v1 audio.set_volume action without execution or session access.
 * issued_at and expires_at are optional and ignored.
 * root is borrowed; command owns its fields and is valid only on success. */
bool mybot_device_command_parse(const mybot_json_t *root, mybot_device_command_t *command);

#endif /* MYBOT_DEVICE_COMMAND_H_ */
