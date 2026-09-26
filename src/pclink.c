#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pclink.h"

#define MAX_FIELDS       32
#define MAX_BOX_FIELDS   6
#define CHUNK_SIZE       512
#define RECEIVE_SIZE     8192
#define TITLE_LENGTH     20
#define MEMO_LENGTH      2000
#define NO_BLOCK         0xffff
#define PACKET_ENQ       0x05
#define PACKET_SYN       0x16
#define PACKET_ACK       0x06
#define PACKET_NAK       0x15
#define ENQ_INTERVAL_MS  250
#define ACK_TIMEOUT_MS   3000
#define IDLE_MS          20000
#define MAX_RETRIES      8

typedef struct {
    const char *id;
    uint8_t     type;
    const char *name;
} box_field_t;

typedef struct {
    char        data_class;
    const char *box;
    const char *singular;
    const char *plural;
    int         field_count;
    box_field_t fields[MAX_BOX_FIELDS];
} box_t;

static const box_t BOXES[] = {
    { 'M', "MEMO.BOX", "memo", "memos", 4,
      { { "ATTR", 6, "Attribute" }, { "DATE", 4, "Date" }, { "TTL1", 1, "Title" }, { "MEM1", 1, "Description" } } },
    { 'D', "SCHEDUL1.BOX", "schedule entry", "schedule entries", 5,
      { { "ATTR", 6, "Attribute" }, { "TIM1", 4, "Start" }, { "TIM2", 4, "End" }, { "ALRM", 4, "Alarm" }, { "MEM1", 1, "Description" } } },
};

typedef struct {
    uint8_t *data;
    size_t   length;
    size_t   capacity;
} buffer_t;

typedef struct {
    size_t   offset;
    size_t   length;
    uint16_t block;
    bool     reply;
} message_t;

typedef enum {
    STAGE_KEYS,
    STAGE_WAIT_START,
    STAGE_SEND_ENQ,
    STAGE_WAIT_ACK,
    STAGE_WAIT_REPLY,
    STAGE_DONE,
    STAGE_FAILED,
} stage_t;

struct pclink {
    buffer_t  messages_data;
    message_t *messages;
    int       message_count;
    int       message_capacity;
    int       current;
    int       retries;
    int       record_count;
    size_t    bytes_total;
    size_t    bytes_done;
    const box_t *box;
    char      description[96];
    char      error[160];
    stage_t   stage;
    uint64_t  active_at;
    uint64_t  stage_at;
    uint64_t  last_enq_at;
    int       key_step;
    uint8_t   received[RECEIVE_SIZE];
    size_t    received_length;
};

static bool append(buffer_t *buffer, const void *data, size_t length) {
    if (buffer->length + length > buffer->capacity) {
        size_t capacity = buffer->capacity ? buffer->capacity * 2 : 4096;
        while (capacity < buffer->length + length) capacity *= 2;
        uint8_t *grown = realloc(buffer->data, capacity);
        if (!grown) return false;
        buffer->data = grown;
        buffer->capacity = capacity;
    }
    memcpy(buffer->data + buffer->length, data, length);
    buffer->length += length;
    return true;
}

static void append_byte(buffer_t *buffer, uint8_t value) {
    append(buffer, &value, 1);
}

static void append_word(buffer_t *buffer, uint16_t value) {
    uint8_t bytes[2] = { (uint8_t)(value >> 8), (uint8_t)value };
    append(buffer, bytes, 2);
}

static void append_long(buffer_t *buffer, uint32_t value) {
    uint8_t bytes[4] = { (uint8_t)(value >> 24), (uint8_t)(value >> 16), (uint8_t)(value >> 8), (uint8_t)value };
    append(buffer, bytes, 4);
}

static const uint8_t *find_text(const uint8_t *data, size_t length, const char *text) {
    size_t text_length = strlen(text);
    for (size_t i = 0; i + text_length <= length; i++) {
        if (memcmp(data + i, text, text_length) == 0) return data + i;
    }
    return NULL;
}

static int split_csv(const uint8_t *line, size_t length, buffer_t *fields, size_t *starts, size_t *lengths, int max_fields) {
    int count = 0;
    size_t index = 0;
    bool quoted = false;
    starts[0] = fields->length;
    while (index <= length && count < max_fields) {
        uint8_t character = index < length ? line[index] : ',';
        if (quoted) {
            bool closes = character == '"' && (index + 1 >= length || line[index + 1] == ',');
            if (closes) quoted = false;
            else if (character == '"' && index + 1 < length && line[index + 1] == '"') {
                append_byte(fields, '"');
                index++;
            } else {
                append_byte(fields, character);
            }
        } else if (character == '"') {
            quoted = true;
        } else if (character == ',') {
            lengths[count] = fields->length - starts[count];
            count++;
            if (count < max_fields) starts[count] = fields->length;
        } else {
            append_byte(fields, character);
        }
        index++;
    }
    return count;
}

static int number_at(const char *digits, int start, int count) {
    int value = 0;
    for (int i = start; i < start + count; i++) value = value * 10 + digits[i] - '0';
    return value;
}

static void encode_value(buffer_t *out, const box_field_t *field, const uint8_t *value, size_t length) {
    if (field->type == 6) {
        append_byte(out, 0x80);
        return;
    }
    if (field->type == 4) {
        char digits[16];
        int count = 0;
        for (size_t i = 0; i < length && count < 12; i++) {
            if (value[i] >= '0' && value[i] <= '9') digits[count++] = (char)value[i];
        }
        if (count < 8) return;
        int year = number_at(digits, 0, 4);
        append_word(out, (uint16_t)year);
        append_byte(out, (uint8_t)number_at(digits, 4, 2));
        append_byte(out, (uint8_t)number_at(digits, 6, 2));
        if (count >= 12) {
            append_byte(out, (uint8_t)number_at(digits, 8, 2));
            append_byte(out, (uint8_t)number_at(digits, 10, 2));
        } else {
            append_word(out, 0xffff);
        }
        append_word(out, 0xffff);
        return;
    }
    if (strcmp(field->id, "TTL1") == 0) {
        for (size_t i = 0; i < TITLE_LENGTH; i++) append_byte(out, i < length ? value[i] : ' ');
        return;
    }
    for (size_t i = 0; i < length && i < MEMO_LENGTH; i++) append_byte(out, value[i] == 0x1f ? '\r' : value[i]);
}

static void encode_item_header(buffer_t *stream, const box_t *box) {
    buffer_t body = { 0 };
    append_word(&body, (uint16_t)box->field_count);
    for (int i = 0; i < box->field_count; i++) {
        append_word(&body, box->fields[i].type);
        append(&body, box->fields[i].id, 4);
        append_word(&body, (uint16_t)strlen(box->fields[i].name));
        append(&body, box->fields[i].name, strlen(box->fields[i].name));
    }
    append(&body, "\xff\xff\r\n", 4);
    append(stream, "\"IT\",", 5);
    append_long(stream, (uint32_t)body.length);
    append(stream, body.data, body.length);
    free(body.data);
}

static void encode_record(buffer_t *stream, const box_t *box, buffer_t *values, size_t *starts, size_t *lengths, const int *columns) {
    buffer_t field_data = { 0 };
    size_t field_lengths[MAX_BOX_FIELDS];
    for (int i = 0; i < box->field_count; i++) {
        size_t before = field_data.length;
        const uint8_t *value = columns[i] >= 0 ? values->data + starts[columns[i]] : (const uint8_t *)"";
        size_t length = columns[i] >= 0 ? lengths[columns[i]] : 0;
        encode_value(&field_data, &box->fields[i], value, length);
        field_lengths[i] = field_data.length - before;
    }
    buffer_t fields = { 0 };
    append_word(&fields, (uint16_t)box->field_count);
    for (int i = 0; i < box->field_count; i++) append_word(&fields, (uint16_t)field_lengths[i]);
    append(&fields, field_data.data, field_data.length);
    buffer_t body = { 0 };
    append_word(&body, 0);
    append_word(&body, 0xffff);
    append_word(&body, 0xff80);
    append_long(&body, (uint32_t)fields.length);
    append(&body, fields.data, fields.length);
    append(&body, "\r\n", 2);
    append(stream, "\"D\",", 4);
    append_long(stream, (uint32_t)body.length);
    append(stream, body.data, body.length);
    free(field_data.data);
    free(fields.data);
    free(body.data);
}

static bool add_message(pclink_t *link, const void *data, size_t length, uint16_t block, bool reply) {
    if (link->message_count == link->message_capacity) {
        int capacity = link->message_capacity ? link->message_capacity * 2 : 64;
        message_t *grown = realloc(link->messages, (size_t)capacity * sizeof *grown);
        if (!grown) return false;
        link->messages = grown;
        link->message_capacity = capacity;
    }
    message_t *message = &link->messages[link->message_count++];
    message->offset = link->messages_data.length;
    message->length = length;
    message->block = block;
    message->reply = reply;
    link->bytes_total += length;
    return append(&link->messages_data, data, length);
}

pclink_t *pclink_create_from_wzd(const uint8_t *data, size_t length, char *error, size_t error_size) {
    error[0] = 0;
    const uint8_t *start = find_text(data, length, "<DATA>\r\n");
    const uint8_t *end = start ? find_text(start, length - (size_t)(start - data), "</DATA>") : NULL;
    if (!start || !end || !find_text(start, (size_t)(end - start), "Sharp Download Data")) {
        snprintf(error, error_size, "no Sharp Download Data in this .wzd");
        return NULL;
    }
    start += 8;
    pclink_t *link = calloc(1, sizeof *link);
    buffer_t stream = { 0 };
    buffer_t cells = { 0 };
    size_t starts[MAX_FIELDS], lengths[MAX_FIELDS];
    size_t id_starts[MAX_FIELDS], id_lengths[MAX_FIELDS];
    buffer_t ids = { 0 };
    int id_count = 0;
    int columns[MAX_BOX_FIELDS];
    for (const uint8_t *line = start; line < end;) {
        const uint8_t *line_end = find_text(line, (size_t)(end - line), "\r\n");
        if (!line_end) line_end = end;
        size_t line_length = (size_t)(line_end - line);
        cells.length = 0;
        int count = split_csv(line, line_length, &cells, starts, lengths, MAX_FIELDS);
        if (count >= 2 && lengths[0] == 1) {
            char kind = (char)cells.data[starts[0]];
            if (kind == 'C' && !link->box) {
                char data_class = lengths[1] ? (char)cells.data[starts[1]] : 0;
                for (size_t i = 0; i < sizeof BOXES / sizeof BOXES[0]; i++) {
                    if (BOXES[i].data_class == data_class) link->box = &BOXES[i];
                }
                if (!link->box) {
                    snprintf(error, error_size, "data class \"%c\" isn't supported yet, only memo and schedule", data_class ? data_class : '?');
                    break;
                }
                append(&stream, "\"F\",\"S1:", 8);
                append(&stream, link->box->box, strlen(link->box->box));
                append(&stream, "\"\r\n", 3);
                encode_item_header(&stream, link->box);
            } else if (kind == 'I' && link->box) {
                ids.length = 0;
                id_count = split_csv(line, line_length, &ids, id_starts, id_lengths, MAX_FIELDS);
                for (int i = 0; i < link->box->field_count; i++) {
                    columns[i] = -1;
                    for (int column = 1; column < id_count; column++) {
                        if (id_lengths[column] == 4 && memcmp(ids.data + id_starts[column], link->box->fields[i].id, 4) == 0) columns[i] = column;
                    }
                }
            } else if (kind == 'D' && link->box && id_count) {
                for (int i = count; i < MAX_FIELDS; i++) lengths[i] = 0;
                int bounded[MAX_BOX_FIELDS];
                for (int i = 0; i < link->box->field_count; i++) bounded[i] = columns[i] < count ? columns[i] : -1;
                encode_record(&stream, link->box, &cells, starts, lengths, bounded);
                link->record_count++;
            }
        }
        line = line_end + 2;
    }
    if (!link->box || !link->record_count) {
        if (!error[0]) snprintf(error, error_size, "no records found in the .wzd data");
        free(stream.data);
        free(cells.data);
        free(ids.data);
        free(link);
        return NULL;
    }
    bool ok = add_message(link, "WSYS RECEIVE WIZ_ALL S1:", 25, NO_BLOCK, true) && add_message(link, "WDAT SEND", 10, NO_BLOCK, false);
    uint16_t block = 1;
    for (size_t offset = 0; ok && offset < stream.length; offset += CHUNK_SIZE) {
        size_t chunk = stream.length - offset < CHUNK_SIZE ? stream.length - offset : CHUNK_SIZE;
        ok = add_message(link, stream.data + offset, chunk, block++, false);
    }
    ok = ok && add_message(link, "\x1a", 1, NO_BLOCK, false) && add_message(link, "WSYS RESET", 11, NO_BLOCK, true);
    free(stream.data);
    free(cells.data);
    free(ids.data);
    if (!ok) {
        snprintf(error, error_size, "out of memory");
        pclink_destroy(link);
        return NULL;
    }
    snprintf(link->description, sizeof link->description, "%d %s", link->record_count, link->record_count == 1 ? link->box->singular : link->box->plural);
    return link;
}

void pclink_destroy(pclink_t *link) {
    if (!link) return;
    free(link->messages_data.data);
    free(link->messages);
    free(link);
}

const char *pclink_describe(pclink_t *link) {
    return link->description;
}

float pclink_progress(pclink_t *link) {
    return link->bytes_total ? (float)link->bytes_done / (float)link->bytes_total : 0.0f;
}

const char *pclink_error(pclink_t *link) {
    return link->error;
}

static void receive_byte(void *context, uint8_t value) {
    pclink_t *link = context;
    if (link->received_length < RECEIVE_SIZE) link->received[link->received_length++] = value;
}

static void send_bytes(machine_t *machine, const uint8_t *data, size_t length) {
    machine_serial_input(machine, data, length);
}

static void send_packet(machine_t *machine, uint8_t code) {
    uint8_t packet[8] = { 0, 0, 0, 0, 0, 0x96, 0x82, code };
    send_bytes(machine, packet, sizeof packet);
}

static void send_frame(pclink_t *link, machine_t *machine, const message_t *message) {
    const uint8_t *payload = link->messages_data.data + message->offset;
    uint16_t checksum = 0;
    for (size_t i = 0; i < message->length; i++) checksum = (uint16_t)(checksum + payload[i]);
    uint8_t header[15] = { 0, 0, 0, 0, 0, 0x96, 0x81, 0x10, (uint8_t)message->block, (uint8_t)(message->block >> 8), 0x01, 0x40, 0xfe,
                           (uint8_t)message->length, (uint8_t)(message->length >> 8) };
    uint8_t trailer[2] = { (uint8_t)checksum, (uint8_t)(checksum >> 8) };
    send_bytes(machine, header, sizeof header);
    send_bytes(machine, payload, message->length);
    send_bytes(machine, trailer, sizeof trailer);
}

static uint64_t ms_to_cycles(uint32_t ms) {
    return (uint64_t)MACHINE_CLOCK_HZ * ms / 1000;
}

static void set_stage(pclink_t *link, machine_t *machine, stage_t stage) {
    link->stage = stage;
    link->stage_at = machine_cycles(machine);
}

static void fail(pclink_t *link, machine_t *machine, const char *reason) {
    snprintf(link->error, sizeof link->error, "%s", reason);
    set_stage(link, machine, STAGE_FAILED);
}

static int next_event(pclink_t *link, const uint8_t **payload, size_t *payload_length) {
    for (;;) {
        size_t start = 0;
        while (start < link->received_length && link->received[start] != 0x96) start++;
        memmove(link->received, link->received + start, link->received_length - start);
        link->received_length -= start;
        if (link->received_length < 3) return -1;
        if (link->received[1] == 0x82) {
            int code = link->received[2];
            memmove(link->received, link->received + 3, link->received_length - 3);
            link->received_length -= 3;
            return code;
        }
        if (link->received[1] != 0x81) {
            memmove(link->received, link->received + 1, link->received_length - 1);
            link->received_length -= 1;
            continue;
        }
        if (link->received_length < 10) return -1;
        size_t length = (size_t)(link->received[8] | link->received[9] << 8);
        if (link->received_length < 10 + length + 2) return -1;
        *payload = link->received + 10;
        *payload_length = length;
        return 0x100;
    }
}

static void consume_frame(pclink_t *link, size_t payload_length) {
    size_t total = 10 + payload_length + 2;
    memmove(link->received, link->received + total, link->received_length - total);
    link->received_length -= total;
}

void pclink_start(pclink_t *link, machine_t *machine) {
    machine_set_serial_output(machine, receive_byte, link);
    link->active_at = machine_cycles(machine);
    link->received_length = 0;
    link->current = 0;
    link->key_step = 0;
    set_stage(link, machine, STAGE_KEYS);
}

static void press_keys(pclink_t *link, machine_t *machine) {
    static const struct { uint32_t at_ms; int column, row; bool down; } steps[] = {
        { 1500, 0, 6, true }, { 1650, 0, 6, false }, { 2100, 1, 6, true }, { 2250, 1, 6, false },
    };
    uint64_t elapsed = machine_cycles(machine) - link->stage_at;
    while (link->key_step < (int)(sizeof steps / sizeof steps[0]) && elapsed >= ms_to_cycles(steps[link->key_step].at_ms)) {
        machine_set_key(machine, steps[link->key_step].column, steps[link->key_step].row, steps[link->key_step].down);
        link->key_step++;
    }
    if (link->key_step == (int)(sizeof steps / sizeof steps[0])) set_stage(link, machine, STAGE_WAIT_START);
}

static void begin_message(pclink_t *link, machine_t *machine) {
    if (link->current >= link->message_count) {
        set_stage(link, machine, STAGE_DONE);
        return;
    }
    link->retries = 0;
    link->last_enq_at = 0;
    set_stage(link, machine, STAGE_SEND_ENQ);
}

pclink_state_t pclink_step(pclink_t *link, machine_t *machine) {
    uint64_t now = machine_cycles(machine);
    if (link->stage != STAGE_DONE && link->stage != STAGE_FAILED && now - link->active_at > ms_to_cycles(IDLE_MS)) {
        fail(link, machine, "timed out waiting for the organizer");
    }
    if (link->stage == STAGE_KEYS) press_keys(link, machine);
    const uint8_t *payload = NULL;
    size_t payload_length = 0;
    int event;
    while (link->stage != STAGE_DONE && link->stage != STAGE_FAILED && (event = next_event(link, &payload, &payload_length)) >= 0) {
        link->active_at = now;
        switch (link->stage) {
            case STAGE_WAIT_START:
                if (event == PACKET_ENQ) send_packet(machine, PACKET_SYN);
                if (event == 0x100) {
                    bool started = payload_length >= 10 && memcmp(payload, "WSYS START", 10) == 0;
                    consume_frame(link, payload_length);
                    send_packet(machine, PACKET_ACK);
                    if (started) begin_message(link, machine);
                }
                break;
            case STAGE_SEND_ENQ:
                if (event == PACKET_SYN) {
                    send_frame(link, machine, &link->messages[link->current]);
                    set_stage(link, machine, STAGE_WAIT_ACK);
                }
                if (event == 0x100) consume_frame(link, payload_length);
                break;
            case STAGE_WAIT_ACK:
                if (event == PACKET_ACK) {
                    link->bytes_done += link->messages[link->current].length;
                    if (link->messages[link->current].reply) {
                        set_stage(link, machine, STAGE_WAIT_REPLY);
                    } else {
                        link->current++;
                        begin_message(link, machine);
                    }
                } else if (event == PACKET_NAK) {
                    if (++link->retries > MAX_RETRIES) fail(link, machine, "the organizer rejected the data");
                    else set_stage(link, machine, STAGE_SEND_ENQ);
                }
                if (event == 0x100) consume_frame(link, payload_length);
                break;
            case STAGE_WAIT_REPLY:
                if (event == PACKET_ENQ) send_packet(machine, PACKET_SYN);
                if (event == 0x100) {
                    bool error_reply = payload_length == 1 && payload[0] == 0x18;
                    consume_frame(link, payload_length);
                    send_packet(machine, PACKET_ACK);
                    if (error_reply) {
                        fail(link, machine, "the organizer reported an error");
                    } else {
                        link->current++;
                        begin_message(link, machine);
                    }
                }
                break;
            default:
                if (event == 0x100) consume_frame(link, payload_length);
                break;
        }
    }
    if (link->stage == STAGE_SEND_ENQ && (!link->last_enq_at || now - link->last_enq_at >= ms_to_cycles(ENQ_INTERVAL_MS))) {
        send_packet(machine, PACKET_ENQ);
        link->last_enq_at = now;
    }
    if (link->stage == STAGE_WAIT_ACK && now - link->stage_at > ms_to_cycles(ACK_TIMEOUT_MS)) {
        if (++link->retries > MAX_RETRIES) fail(link, machine, "no acknowledgement from the organizer");
        else set_stage(link, machine, STAGE_SEND_ENQ);
    }
    if (link->stage == STAGE_DONE) return PCLINK_DONE;
    if (link->stage == STAGE_FAILED) return PCLINK_FAILED;
    return PCLINK_RUNNING;
}
