#include "pim/net.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ---- NTP ---- */

#define NTP_UNIX_OFFSET 2208988800U

void pim_ntp_request(uint8_t packet[PIM_NTP_PACKET_SIZE]) {
    memset(packet, 0, PIM_NTP_PACKET_SIZE);
    packet[0] = 0x23; /* leap 0, version 4, client mode */
}

bool pim_ntp_decode(const uint8_t *packet, size_t length, uint32_t *unix_seconds) {
    uint32_t seconds;
    if (packet == NULL || unix_seconds == NULL || length < PIM_NTP_PACKET_SIZE) return false;
    if ((packet[0] & 7U) != 4U || packet[1] == 0 || (packet[0] >> 6) == 3U) return false;
    seconds = ((uint32_t)packet[40] << 24) | ((uint32_t)packet[41] << 16) | ((uint32_t)packet[42] << 8) | packet[43];
    if (seconds < NTP_UNIX_OFFSET) return false;
    *unix_seconds = seconds - NTP_UNIX_OFFSET;
    return true;
}

bool pim_tz_valid(int tz_minutes) { return tz_minutes >= -720 && tz_minutes <= 840 && tz_minutes % 15 == 0; }

bool pim_time_from_unix(uint32_t unix_seconds, int tz_minutes, pim_date_t *date, uint16_t *minutes) {
    int64_t local = (int64_t)unix_seconds + (int64_t)tz_minutes * 60;
    int64_t days, z, era, doe, yoe, year, doy, mp, day, month;
    if (date == NULL || minutes == NULL || local < 0 || !pim_tz_valid(tz_minutes)) return false;
    days = local / 86400;
    *minutes = (uint16_t)(local % 86400 / 60);
    z = days + 719468; /* civil-from-days, proleptic Gregorian */
    era = z / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    year = yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    day = doy - (153 * mp + 2) / 5 + 1;
    month = mp < 10 ? mp + 3 : mp - 9;
    if (month <= 2) year++;
    if (year < 2000 || year > 2099) return false;
    *date = (pim_date_t){(uint16_t)year, (uint8_t)month, (uint8_t)day};
    return true;
}

/* ---- Wi-Fi configuration ---- */

void pim_wifi_config_init(pim_wifi_config_t *config) {
    memset(config, 0, sizeof(*config));
    config->tz_minutes = 330;
}

static void copy_text(char *destination, size_t size, const char *source) {
    size_t length = strlen(source);
    if (length >= size) length = size - 1;
    memcpy(destination, source, length);
    destination[length] = '\0';
}

static bool config_text_ok(const char *text, size_t max, bool allow_empty) {
    size_t length = strlen(text);
    if (length > max || (length == 0 && !allow_empty)) return false;
    for (size_t i = 0; i < length; i++)
        if (text[i] == '\t' || text[i] == '\n' || text[i] == '\r') return false;
    return true;
}

bool pim_wifi_config_remember(pim_wifi_config_t *config, const char *ssid, const char *password) {
    size_t position = config->count;
    if (!config_text_ok(ssid, PIM_WIFI_SSID_MAX, false) || !config_text_ok(password, PIM_WIFI_PASSWORD_MAX, false))
        return false;
    for (size_t i = 0; i < config->count; i++)
        if (strcmp(config->networks[i].ssid, ssid) == 0) { position = i; break; }
    if (position == config->count) {
        if (config->count == PIM_WIFI_MAX_NETWORKS) position = config->count - 1;
        else config->count++;
    }
    memmove(&config->networks[1], &config->networks[0], position * sizeof(config->networks[0]));
    copy_text(config->networks[0].ssid, sizeof(config->networks[0].ssid), ssid);
    copy_text(config->networks[0].password, sizeof(config->networks[0].password), password);
    return true;
}

bool pim_wifi_config_forget(pim_wifi_config_t *config, size_t index) {
    if (index >= config->count) return false;
    memmove(&config->networks[index], &config->networks[index + 1],
            (config->count - index - 1) * sizeof(config->networks[0]));
    config->count--;
    return true;
}

const pim_wifi_network_t *pim_wifi_config_find(const pim_wifi_config_t *config, const char *ssid) {
    for (size_t i = 0; i < config->count; i++)
        if (strcmp(config->networks[i].ssid, ssid) == 0) return &config->networks[i];
    return NULL;
}

bool pim_wifi_config_load(const char *path, pim_wifi_config_t *config) {
    char line[160];
    FILE *file;
    pim_wifi_config_init(config);
    file = fopen(path, "rb");
    if (file == NULL) return false;
    while (fgets(line, sizeof(line), file) != NULL) {
        size_t length = strlen(line);
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) line[--length] = '\0';
        if (strncmp(line, "tz=", 3) == 0) {
            int value = atoi(line + 3);
            if (pim_tz_valid(value)) config->tz_minutes = (int16_t)value;
        } else if (strncmp(line, "net=", 4) == 0 && config->count < PIM_WIFI_MAX_NETWORKS) {
            char *separator = strchr(line + 4, '\t');
            if (separator == NULL) continue;
            *separator = '\0';
            if (!config_text_ok(line + 4, PIM_WIFI_SSID_MAX, false) ||
                !config_text_ok(separator + 1, PIM_WIFI_PASSWORD_MAX, false)) continue;
            copy_text(config->networks[config->count].ssid, sizeof(config->networks[config->count].ssid), line + 4);
            copy_text(config->networks[config->count].password, sizeof(config->networks[config->count].password), separator + 1);
            config->count++;
        }
    }
    fclose(file);
    return true;
}

bool pim_wifi_config_save(const char *path, const pim_wifi_config_t *config) {
    char temporary[160];
    FILE *file;
    bool ok = true;
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    file = fopen(temporary, "wb");
    if (file == NULL) return false;
    ok = fprintf(file, "tz=%d\n", config->tz_minutes) > 0;
    for (size_t i = 0; i < config->count && ok; i++)
        ok = fprintf(file, "net=%s\t%s\n", config->networks[i].ssid, config->networks[i].password) > 0;
    if (fclose(file) != 0 || !ok) { remove(temporary); return false; }
    remove(path);
    return rename(temporary, path) == 0;
}

/* ---- iCalendar ---- */

typedef struct { char *out; size_t size, length; bool overflow; } sink_t;

static void put(sink_t *sink, const char *text) {
    size_t length = strlen(text);
    if (sink->overflow || sink->length + length + 1 > sink->size) { sink->overflow = true; return; }
    memcpy(sink->out + sink->length, text, length + 1);
    sink->length += length;
}
static void putf(sink_t *sink, const char *format, ...) __attribute__((format(printf, 2, 3)));
static void putf(sink_t *sink, const char *format, ...) {
    char line[320];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    put(sink, line);
}
static void put_text_property(sink_t *sink, const char *name, const char *value) {
    char escaped[300];
    size_t length = 0;
    for (const char *c = value; *c != '\0' && length + 3 < sizeof(escaped); c++) {
        if (*c == '\\' || *c == ',' || *c == ';') escaped[length++] = '\\';
        if (*c == '\n') { escaped[length++] = '\\'; escaped[length++] = 'n'; }
        else if (*c != '\r') escaped[length++] = *c;
    }
    escaped[length] = '\0';
    putf(sink, "%s:%s\r\n", name, escaped);
}
static void put_datetime(sink_t *sink, const char *name, pim_date_t date, unsigned minutes) {
    if (minutes >= 1440) { date = pim_date_shift(date, (int)(minutes / 1440)); minutes %= 1440; }
    putf(sink, "%s:%04u%02u%02uT%02u%02u00\r\n", name, date.year, date.month, date.day, minutes / 60, minutes % 60);
}

size_t pim_ics_export(const pim_database_t *database, char *out, size_t out_size) {
    sink_t sink = {out, out_size, 0, false};
    if (out_size == 0) return 0;
    out[0] = '\0';
    put(&sink, "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//PicoCalc PIM//EN\r\n");
    for (size_t i = 0; i < database->appointment_count; i++) {
        const pim_appointment_t *item = &database->appointments[i];
        putf(&sink, "BEGIN:VEVENT\r\nUID:appt-%lu@picocalc\r\n", (unsigned long)item->id);
        put_datetime(&sink, "DTSTAMP", item->date, item->minutes);
        put_datetime(&sink, "DTSTART", item->date, item->minutes);
        if (item->duration > 0) put_datetime(&sink, "DTEND", item->date, (unsigned)item->minutes + item->duration);
        put_text_property(&sink, "SUMMARY", item->title);
        if (item->description[0] != '\0') put_text_property(&sink, "DESCRIPTION", item->description);
        put(&sink, "END:VEVENT\r\n");
    }
    for (size_t i = 0; i < database->todo_count; i++) {
        const pim_todo_t *item = &database->todos[i];
        putf(&sink, "BEGIN:VTODO\r\nUID:todo-%lu@picocalc\r\nDTSTAMP:19700101T000000Z\r\n", (unsigned long)item->id);
        put_text_property(&sink, "SUMMARY", item->title);
        putf(&sink, "STATUS:%s\r\nPRIORITY:%d\r\nEND:VTODO\r\n", item->completed ? "COMPLETED" : "NEEDS-ACTION",
             item->priority == PIM_PRIORITY_HIGH ? 1 : item->priority == PIM_PRIORITY_LOW ? 9 : 5);
    }
    put(&sink, "END:VCALENDAR\r\n");
    return sink.overflow ? 0 : sink.length;
}

static void unescape_text(char *out, size_t size, const char *value) {
    size_t length = 0;
    for (; *value != '\0' && length + 1 < size; value++) {
        if (*value == '\\' && value[1] != '\0') {
            value++;
            out[length++] = (*value == 'n' || *value == 'N') ? '\n' : *value;
        } else if (*value != '\r') out[length++] = *value;
    }
    out[length] = '\0';
}

static bool parse_datetime(const char *value, pim_date_t *date, unsigned *minutes, bool *date_only) {
    unsigned year, month, day, hour = 0, minute = 0;
    size_t digits = 0;
    while (isdigit((unsigned char)value[digits])) digits++;
    if (digits != 8 || sscanf(value, "%4u%2u%2u", &year, &month, &day) != 3) return false;
    *date_only = value[8] != 'T';
    if (!*date_only && sscanf(value + 9, "%2u%2u", &hour, &minute) != 2) return false;
    if (hour > 23 || minute > 59 || year < 2000 || year > 2099) return false;
    *date = (pim_date_t){(uint16_t)year, (uint8_t)month, (uint8_t)day};
    *minutes = hour * 60 + minute;
    return pim_date_is_valid(*date);
}

static int days_between(pim_date_t from, pim_date_t to) {
    int days = 0;
    pim_date_t cursor = from;
    while (pim_date_compare(cursor, to) < 0 && days < 400) { cursor = pim_date_shift(cursor, 1); days++; }
    return days;
}

typedef struct {
    bool in_event, in_todo, have_start, start_date_only, have_end;
    pim_date_t start_date, end_date;
    unsigned start_minutes, end_minutes;
    char summary[64], description[128];
    bool completed;
    int priority;
} ics_item_t;

static void finish_item(pim_database_t *database, ics_item_t *item, pim_ics_result_t *result) {
    if (item->in_event) {
        pim_appointment_t appointment;
        bool duplicate = false;
        memset(&appointment, 0, sizeof(appointment));
        if (!item->have_start || item->summary[0] == '\0') { result->rejected++; return; }
        appointment.date = item->start_date;
        appointment.minutes = item->start_date_only ? 0 : (uint16_t)item->start_minutes;
        if (item->have_end && !item->start_date_only) {
            int total = days_between(item->start_date, item->end_date) * 1440 + (int)item->end_minutes - (int)item->start_minutes;
            if (total > 0) appointment.duration = (uint16_t)(total > 1440 ? 1440 : total);
        }
        if (appointment.minutes + appointment.duration > 1440) appointment.duration = (uint16_t)(1440 - appointment.minutes);
        copy_text(appointment.title, sizeof(appointment.title), item->summary);
        copy_text(appointment.description, sizeof(appointment.description), item->description);
        for (size_t i = 0; i < database->appointment_count; i++) {
            const pim_appointment_t *existing = &database->appointments[i];
            if (pim_date_compare(existing->date, appointment.date) == 0 && existing->minutes == appointment.minutes &&
                strcmp(existing->title, appointment.title) == 0) duplicate = true;
        }
        if (duplicate) result->skipped++;
        else if (pim_add_appointment(database, &appointment)) result->events++;
        else result->rejected++;
    } else if (item->in_todo) {
        pim_todo_t todo;
        bool duplicate = false;
        memset(&todo, 0, sizeof(todo));
        if (item->summary[0] == '\0') { result->rejected++; return; }
        todo.completed = item->completed;
        todo.priority = item->priority >= 1 && item->priority <= 4 ? PIM_PRIORITY_HIGH
                        : item->priority >= 6 ? PIM_PRIORITY_LOW : PIM_PRIORITY_NORMAL;
        copy_text(todo.title, sizeof(todo.title), item->summary);
        for (size_t i = 0; i < database->todo_count; i++)
            if (strcmp(database->todos[i].title, todo.title) == 0) duplicate = true;
        if (duplicate) result->skipped++;
        else if (pim_add_todo(database, &todo)) result->todos++;
        else result->rejected++;
    }
}

pim_ics_result_t pim_ics_import(pim_database_t *database, const char *text) {
    pim_ics_result_t result = {0, 0, 0, 0};
    ics_item_t item;
    char line[400];
    const char *cursor = text;
    memset(&item, 0, sizeof(item));
    while (cursor != NULL && *cursor != '\0') {
        size_t length = 0;
        char *colon, *name_end;
        /* Read one logical line, joining folded continuation lines. */
        for (;;) {
            const char *end = strpbrk(cursor, "\r\n");
            size_t part = end == NULL ? strlen(cursor) : (size_t)(end - cursor);
            if (length + part < sizeof(line)) { memcpy(line + length, cursor, part); length += part; }
            cursor += part;
            if (*cursor == '\r') cursor++;
            if (*cursor == '\n') cursor++;
            if (*cursor != ' ' && *cursor != '\t') break;
            cursor++;
        }
        line[length] = '\0';
        colon = strchr(line, ':');
        if (colon == NULL) continue;
        *colon++ = '\0';
        name_end = strchr(line, ';');
        if (name_end != NULL) *name_end = '\0';
        for (char *c = line; *c != '\0'; c++) *c = (char)toupper((unsigned char)*c);
        if (strcmp(line, "BEGIN") == 0) {
            memset(&item, 0, sizeof(item));
            item.in_event = strcasecmp(colon, "VEVENT") == 0;
            item.in_todo = strcasecmp(colon, "VTODO") == 0;
        } else if (strcmp(line, "END") == 0) {
            if (strcasecmp(colon, "VEVENT") == 0 || strcasecmp(colon, "VTODO") == 0) finish_item(database, &item, &result);
            memset(&item, 0, sizeof(item));
        } else if (item.in_event || item.in_todo) {
            if (strcmp(line, "SUMMARY") == 0) unescape_text(item.summary, sizeof(item.summary), colon);
            else if (strcmp(line, "DESCRIPTION") == 0) unescape_text(item.description, sizeof(item.description), colon);
            else if (strcmp(line, "DTSTART") == 0) {
                bool date_only;
                item.have_start = parse_datetime(colon, &item.start_date, &item.start_minutes, &date_only);
                item.start_date_only = date_only;
            } else if (strcmp(line, "DTEND") == 0) {
                bool date_only;
                item.have_end = parse_datetime(colon, &item.end_date, &item.end_minutes, &date_only);
            } else if (strcmp(line, "STATUS") == 0) item.completed = strcasecmp(colon, "COMPLETED") == 0;
            else if (strcmp(line, "PERCENT-COMPLETE") == 0) item.completed = atoi(colon) >= 100;
            else if (strcmp(line, "PRIORITY") == 0) item.priority = atoi(colon);
        }
    }
    return result;
}
