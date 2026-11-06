#include "pim/web.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pim/net.h"
#include "pim/storage.h"

#ifdef PIM_VFS_DIRENT
#include <sys/dirent.h>
extern DIR *opendir(const char *path);
extern struct dirent *readdir(DIR *directory);
extern int closedir(DIR *directory);
#else
#include <dirent.h>
#endif

typedef struct { char *out; size_t size, length; bool overflow; } page_t;

static void put(page_t *page, const char *text) {
    size_t length = strlen(text);
    if (page->overflow || page->length + length + 1 > page->size) { page->overflow = true; return; }
    memcpy(page->out + page->length, text, length + 1);
    page->length += length;
}
static void putf(page_t *page, const char *format, ...) __attribute__((format(printf, 2, 3)));
static void putf(page_t *page, const char *format, ...) {
    char line[256];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    put(page, line);
}
static void put_escaped(page_t *page, const char *text) {
    char piece[2] = {0, 0};
    for (; *text != '\0'; text++) {
        switch (*text) {
            case '&': put(page, "&amp;"); break;
            case '<': put(page, "&lt;"); break;
            case '>': put(page, "&gt;"); break;
            case '"': put(page, "&quot;"); break;
            case '\'': put(page, "&#39;"); break;
            default: piece[0] = *text; put(page, piece);
        }
    }
}

static void copy_text(char *destination, size_t size, const char *source) {
    size_t length = strlen(source);
    if (length >= size) length = size - 1;
    memcpy(destination, source, length);
    destination[length] = '\0';
}

/* Reads one URL-encoded parameter (query string or form body); CR is dropped so textareas keep plain LF. */
static bool param_get(const char *params, const char *key, char *out, size_t size) {
    size_t key_length = strlen(key);
    const char *cursor = params;
    while (cursor != NULL && *cursor != '\0') {
        const char *end = strchr(cursor, '&');
        size_t span = end == NULL ? strlen(cursor) : (size_t)(end - cursor);
        if (span > key_length && strncmp(cursor, key, key_length) == 0 && cursor[key_length] == '=') {
            size_t length = 0;
            for (const char *c = cursor + key_length + 1; c < cursor + span; c++) {
                char value = *c;
                if (value == '+') value = ' ';
                else if (value == '%' && c + 2 < cursor + span && isxdigit((unsigned char)c[1]) && isxdigit((unsigned char)c[2])) {
                    char hex[3] = {c[1], c[2], 0};
                    value = (char)strtol(hex, NULL, 16);
                    c += 2;
                }
                if (value == '\r') continue;
                if (length + 1 < size) out[length++] = value;
            }
            out[length] = '\0';
            return true;
        }
        cursor = end == NULL ? NULL : end + 1;
    }
    if (size > 0) out[0] = '\0';
    return false;
}

static bool parse_date(const char *text, pim_date_t *date) {
    unsigned day, month, year;
    if (sscanf(text, "%u-%u-%u", &day, &month, &year) != 3 || year > 2099) return false;
    *date = (pim_date_t){(uint16_t)year, (uint8_t)month, (uint8_t)day};
    return pim_date_is_valid(*date);
}
static bool parse_time(const char *text, unsigned *minutes) {
    unsigned hour, minute;
    if (sscanf(text, "%u:%u", &hour, &minute) != 2 || hour > 23 || minute > 59) return false;
    *minutes = hour * 60 + minute;
    return true;
}

static void commit(pim_web_t *web) {
    web->changed = true;
    if (web->storage_path != NULL) {
        pim_storage_save(web->storage_path, web->database);
        pim_storage_export(web->root, web->database);
    }
}

static void set_response(pim_web_response_t *response, int status, const char *type, size_t length) {
    response->status = status;
    response->type = type;
    response->length = length;
}
static void redirect(pim_web_response_t *response, const char *location) {
    set_response(response, 303, "text/plain", 0);
    snprintf(response->location, sizeof(response->location), "%s", location);
}
static void fail(pim_web_response_t *response, char *out, size_t out_size, int status, const char *text) {
    snprintf(out, out_size, "%s\n", text);
    set_response(response, status, "text/plain", strlen(out));
}

static const char *const style =
    "<style>body{background:#000;color:#ffb000;font:16px monospace;max-width:720px;margin:0 auto;padding:8px}"
    "a{color:#ffb000}input,textarea,select,button{background:#000;color:#ffb000;border:1px solid #ffb000;"
    "font:inherit;padding:4px;max-width:100%;box-sizing:border-box}textarea{width:100%}li{margin:8px 0}"
    ".d{opacity:.6;text-decoration:line-through}nav a{margin-right:12px}form{display:inline}</style>";

static void page_begin(page_t *page, const char *title) {
    put(page, "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
              "<title>PicoCalc PIM</title>");
    put(page, style);
    put(page, "<nav><a href=/>Home</a><a href=/agenda>Agenda</a><a href=/todos>To-dos</a><a href=/notes>Notes</a>"
              "<a href=/journal>Journal</a><a href=/files>Files</a><a href=/import>Calendar</a></nav><h2>");
    put_escaped(page, title);
    put(page, "</h2>");
}

static void format_date(char *out, size_t size, pim_date_t date) {
    snprintf(out, size, "%02u-%02u-%04u", date.day, date.month, date.year);
}

static bool name_ok(const char *name) {
    size_t length = strlen(name);
    static const char *const reserved[] = {"pim.dat", "wifi.cfg", "theme.cfg", "today.cfg"};
    if (length == 0 || length > 47 || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return false;
    for (size_t i = 0; i < length; i++)
        if (!isalnum((unsigned char)name[i]) && name[i] != '.' && name[i] != '_' && name[i] != '-') return false;
    for (size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i++)
        if (strcmp(name, reserved[i]) == 0) return false;
    return true;
}

static size_t finish_page(page_t *page, pim_web_response_t *response) {
    if (page->overflow) {
        page->out[page->size - 1] = '\0';
        set_response(response, 200, "text/html", strlen(page->out));
    } else set_response(response, 200, "text/html", page->length);
    return page->length;
}

/* ---- pages ---- */

static void page_login(page_t *page, pim_web_response_t *response, bool locked) {
    page_begin(page, "PicoCalc PIM");
    if (locked) put(page, "<p>Too many wrong PINs. Reconnect Wi-Fi on the device to try again.</p>");
    else put(page, "<p>Enter the PIN shown on the PicoCalc screen.</p>"
                   "<form method=post action=/login><input name=pin inputmode=numeric autofocus> <button>Log in</button></form>");
    finish_page(page, response);
}

static void page_home(pim_web_t *web, page_t *page, pim_web_response_t *response) {
    char date[16];
    size_t open = 0, indices[PIM_MAX_APPOINTMENTS], count;
    for (size_t i = 0; i < web->database->todo_count; i++) if (!web->database->todos[i].completed) open++;
    format_date(date, sizeof(date), web->today);
    page_begin(page, "PicoCalc PIM");
    putf(page, "<p>Now: %s %02u:%02u</p>", date, web->minutes / 60, web->minutes % 60);
    count = pim_appointments_on(web->database, web->today, indices, PIM_MAX_APPOINTMENTS);
    putf(page, "<h3>Today (%u)</h3><ul>", (unsigned)count);
    for (size_t i = 0; i < count && i < PIM_MAX_APPOINTMENTS; i++) {
        const pim_appointment_t *item = &web->database->appointments[indices[i]];
        putf(page, "<li>%02u:%02u ", item->minutes / 60, item->minutes % 60);
        put_escaped(page, item->title);
        put(page, "</li>");
    }
    putf(page, "</ul><p>%u open to-dos, %u notes, %u journal entries.</p>"
               "<p><a href=/calendar.ics>Export calendar (.ics)</a></p>",
         (unsigned)open, (unsigned)web->database->note_count, (unsigned)web->database->journal_count);
    finish_page(page, response);
}

static void page_agenda(pim_web_t *web, page_t *page, pim_web_response_t *response, const char *query) {
    char text[24], previous[16], next[16];
    pim_date_t date = web->today;
    size_t indices[PIM_MAX_APPOINTMENTS], count;
    if (param_get(query, "date", text, sizeof(text))) parse_date(text, &date);
    page_begin(page, "Agenda");
    format_date(text, sizeof(text), date);
    format_date(previous, sizeof(previous), pim_date_shift(date, -1));
    format_date(next, sizeof(next), pim_date_shift(date, 1));
    putf(page, "<p><a href='/agenda?date=%s'>&lt;</a> <b>%s</b> <a href='/agenda?date=%s'>&gt;</a></p><ul>", previous, text, next);
    count = pim_appointments_on(web->database, date, indices, PIM_MAX_APPOINTMENTS);
    for (size_t i = 0; i < count; i++) {
        const pim_appointment_t *item = &web->database->appointments[indices[i]];
        unsigned end = item->minutes + item->duration;
        putf(page, "<li>%02u:%02u", item->minutes / 60, item->minutes % 60);
        if (item->duration > 0) putf(page, "-%02u:%02u", end / 60, end % 60);
        put(page, " <b>");
        put_escaped(page, item->title);
        put(page, "</b>");
        if (item->description[0] != '\0') { put(page, "<br>"); put_escaped(page, item->description); }
        putf(page, " <form method=post action=/agenda/delete><input type=hidden name=id value=%lu>"
                   "<input type=hidden name=date value=%s><button>Delete</button></form></li>", (unsigned long)item->id, text);
    }
    if (count == 0) put(page, "<li>Nothing scheduled.</li>");
    putf(page, "</ul><h3>Add</h3><form method=post action=/agenda/add>"
               "Date <input name=date value=%s size=10> Time <input name=time value='09:00' size=5> "
               "Minutes <input name=duration value=60 size=4><br>"
               "Title <input name=title maxlength=47 required><br>Notes <input name=description maxlength=119 size=40><br>"
               "<button>Add</button></form>", text);
    finish_page(page, response);
}

static void do_agenda_add(pim_web_t *web, const char *body, pim_web_response_t *response, char *out, size_t out_size) {
    char date_text[24], time_text[16], duration_text[16], location[48];
    pim_appointment_t item;
    unsigned minutes;
    memset(&item, 0, sizeof(item));
    param_get(body, "date", date_text, sizeof(date_text));
    param_get(body, "time", time_text, sizeof(time_text));
    param_get(body, "duration", duration_text, sizeof(duration_text));
    param_get(body, "title", item.title, sizeof(item.title));
    param_get(body, "description", item.description, sizeof(item.description));
    if (!parse_date(date_text, &item.date) || !parse_time(time_text, &minutes) || atoi(duration_text) < 0 ||
        atoi(duration_text) > 1440) { fail(response, out, out_size, 400, "Invalid date, time or duration"); return; }
    item.minutes = (uint16_t)minutes;
    item.duration = (uint16_t)atoi(duration_text);
    if (!pim_add_appointment(web->database, &item)) { fail(response, out, out_size, 400, "Could not add (full, empty title or past midnight)"); return; }
    commit(web);
    snprintf(location, sizeof(location), "/agenda?date=%02u-%02u-%04u", item.date.day, item.date.month, item.date.year);
    redirect(response, location);
}

static void do_agenda_delete(pim_web_t *web, const char *body, pim_web_response_t *response) {
    char text[24], location[48];
    unsigned long id;
    param_get(body, "id", text, sizeof(text));
    id = strtoul(text, NULL, 10);
    for (size_t i = 0; i < web->database->appointment_count; i++)
        if (web->database->appointments[i].id == id) { pim_delete_appointment(web->database, i); commit(web); break; }
    param_get(body, "date", text, sizeof(text));
    snprintf(location, sizeof(location), "/agenda?date=%.12s", text);
    redirect(response, location);
}

static const char *const priority_names[] = {"low", "normal", "high"};

static void page_todos(pim_web_t *web, page_t *page, pim_web_response_t *response) {
    page_begin(page, "To-dos");
    put(page, "<form method=post action=/todos/add><input name=title maxlength=63 size=30 required> "
              "<select name=priority><option value=1>normal</option><option value=2>high</option><option value=0>low</option></select> "
              "<button>Add</button></form><ul>");
    for (size_t i = 0; i < web->database->todo_count; i++) {
        const pim_todo_t *item = &web->database->todos[i];
        putf(page, "<li%s>[%s] ", item->completed ? " class=d" : "", priority_names[item->priority]);
        put_escaped(page, item->title);
        putf(page, " <form method=post action=/todos/toggle><input type=hidden name=id value=%lu><button>%s</button></form> "
                   "<form method=post action=/todos/delete><input type=hidden name=id value=%lu><button>Delete</button></form></li>",
             (unsigned long)item->id, item->completed ? "Undo" : "Done", (unsigned long)item->id);
    }
    if (web->database->todo_count == 0) put(page, "<li>No to-dos.</li>");
    put(page, "</ul>");
    finish_page(page, response);
}

static void do_todo(pim_web_t *web, const char *path, const char *body, pim_web_response_t *response, char *out, size_t out_size) {
    char text[80];
    unsigned long id;
    if (strcmp(path, "/todos/add") == 0) {
        pim_todo_t todo;
        memset(&todo, 0, sizeof(todo));
        param_get(body, "title", todo.title, sizeof(todo.title));
        param_get(body, "priority", text, sizeof(text));
        todo.priority = (pim_priority_t)atoi(text);
        if (!pim_add_todo(web->database, &todo)) { fail(response, out, out_size, 400, "Could not add (full or empty title)"); return; }
        commit(web);
    } else {
        param_get(body, "id", text, sizeof(text));
        id = strtoul(text, NULL, 10);
        for (size_t i = 0; i < web->database->todo_count; i++) {
            if (web->database->todos[i].id != id) continue;
            if (strcmp(path, "/todos/toggle") == 0) pim_toggle_todo(web->database, i);
            else pim_delete_todo(web->database, i);
            commit(web);
            break;
        }
    }
    redirect(response, "/todos");
}

static void page_notes(pim_web_t *web, page_t *page, pim_web_response_t *response) {
    page_begin(page, "Notes");
    put(page, "<p><a href='/note?id=new'>+ New note</a></p><ul>");
    for (size_t i = 0; i < web->database->note_count; i++) {
        putf(page, "<li><a href='/note?id=%lu'>", (unsigned long)web->database->notes[i].id);
        put_escaped(page, web->database->notes[i].title);
        put(page, "</a></li>");
    }
    if (web->database->note_count == 0) put(page, "<li>No notes.</li>");
    put(page, "</ul>");
    finish_page(page, response);
}

static void page_note(pim_web_t *web, page_t *page, pim_web_response_t *response, const char *query) {
    char text[16];
    const pim_note_t *note = NULL;
    param_get(query, "id", text, sizeof(text));
    if (strcmp(text, "new") != 0)
        for (size_t i = 0; i < web->database->note_count; i++)
            if (web->database->notes[i].id == strtoul(text, NULL, 10)) note = &web->database->notes[i];
    page_begin(page, note != NULL ? "Edit note" : "New note");
    putf(page, "<form method=post action=/note/save><input type=hidden name=id value='%s'>Title <input name=title maxlength=47 size=40 value='", note ? text : "");
    if (note != NULL) put_escaped(page, note->title);
    put(page, "' required><br><textarea name=content rows=12 maxlength=383>");
    if (note != NULL) put_escaped(page, note->content);
    put(page, "</textarea><br><button>Save</button></form>");
    if (note != NULL)
        putf(page, " <form method=post action=/note/delete><input type=hidden name=id value=%s><button>Delete</button></form>", text);
    finish_page(page, response);
}

static void do_note(pim_web_t *web, const char *path, const char *body, pim_web_response_t *response, char *out, size_t out_size) {
    char text[16];
    pim_note_t note;
    unsigned long id;
    memset(&note, 0, sizeof(note));
    param_get(body, "id", text, sizeof(text));
    id = strtoul(text, NULL, 10);
    if (strcmp(path, "/note/delete") == 0) {
        for (size_t i = 0; i < web->database->note_count; i++)
            if (web->database->notes[i].id == id) { pim_delete_note(web->database, i); commit(web); break; }
    } else {
        bool ok = false;
        param_get(body, "title", note.title, sizeof(note.title));
        param_get(body, "content", note.content, sizeof(note.content));
        if (text[0] == '\0') ok = pim_add_note(web->database, &note);
        else for (size_t i = 0; i < web->database->note_count; i++)
            if (web->database->notes[i].id == id) { note.created_at = web->database->notes[i].created_at; ok = pim_update_note(web->database, i, &note); break; }
        if (!ok) { fail(response, out, out_size, 400, "Could not save (full or empty title)"); return; }
        commit(web);
    }
    redirect(response, "/notes");
}

static void page_journal(pim_web_t *web, page_t *page, pim_web_response_t *response) {
    char date[16];
    format_date(date, sizeof(date), web->today);
    page_begin(page, "Journal");
    putf(page, "<p><a href='/journal/edit?date=%s'>+ Today's entry (%s)</a></p><ul>", date, date);
    for (size_t i = 0; i < web->database->journal_count; i++) {
        const pim_journal_entry_t *entry = &web->database->journal[i];
        format_date(date, sizeof(date), entry->date);
        putf(page, "<li><a href='/journal/edit?date=%s'>%s</a> (%s)</li>", date, date, pim_mood_name(entry->mood));
    }
    if (web->database->journal_count == 0) put(page, "<li>No entries.</li>");
    put(page, "</ul>");
    finish_page(page, response);
}

static void page_journal_edit(pim_web_t *web, page_t *page, pim_web_response_t *response, const char *query) {
    char text[24];
    pim_date_t date = web->today;
    const pim_journal_entry_t *entry = NULL;
    if (param_get(query, "date", text, sizeof(text))) parse_date(text, &date);
    format_date(text, sizeof(text), date);
    for (size_t i = 0; i < web->database->journal_count; i++)
        if (pim_date_compare(web->database->journal[i].date, date) == 0) entry = &web->database->journal[i];
    page_begin(page, "Journal entry");
    putf(page, "<form method=post action=/journal/save><input type=hidden name=date value=%s><b>%s</b> Mood <select name=mood>", text, text);
    for (int mood = PIM_MOOD_GREAT; mood <= PIM_MOOD_TERRIBLE; mood++)
        putf(page, "<option value=%d%s>%s</option>", mood, entry != NULL && (int)entry->mood == mood ? " selected" : "", pim_mood_name((pim_mood_t)mood));
    put(page, "</select><br><textarea name=content rows=14 maxlength=383>");
    if (entry != NULL) put_escaped(page, entry->content);
    put(page, "</textarea><br><button>Save</button></form>");
    if (entry != NULL)
        putf(page, " <form method=post action=/journal/delete><input type=hidden name=date value=%s><button>Delete</button></form>", text);
    finish_page(page, response);
}

static void do_journal(pim_web_t *web, const char *path, const char *body, pim_web_response_t *response, char *out, size_t out_size) {
    char text[24];
    pim_journal_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    param_get(body, "date", text, sizeof(text));
    if (!parse_date(text, &entry.date)) { fail(response, out, out_size, 400, "Invalid date"); return; }
    if (strcmp(path, "/journal/delete") == 0) {
        for (size_t i = 0; i < web->database->journal_count; i++)
            if (pim_date_compare(web->database->journal[i].date, entry.date) == 0) { pim_delete_journal(web->database, i); commit(web); break; }
    } else {
        param_get(body, "mood", text, sizeof(text));
        entry.mood = (pim_mood_t)atoi(text);
        param_get(body, "content", entry.content, sizeof(entry.content));
        if (!pim_add_or_update_journal(web->database, &entry)) { fail(response, out, out_size, 400, "Could not save (full, empty text or bad mood)"); return; }
        commit(web);
    }
    redirect(response, "/journal");
}

static void join_path(char *out, size_t size, const char *root, const char *name) { snprintf(out, size, "%s/%s", root, name); }

static void page_files(pim_web_t *web, page_t *page, pim_web_response_t *response) {
    DIR *directory;
    struct dirent *entry;
    page_begin(page, "Files");
    put(page, "<form method=get action=/edit>New or open: <input name=name maxlength=47 required> <button>Edit</button></form><ul>");
    directory = opendir(web->root);
    while (directory != NULL && (entry = readdir(directory)) != NULL) {
        char path[320];
        struct stat info;
        if (!name_ok(entry->d_name)) continue;
        join_path(path, sizeof(path), web->root, entry->d_name);
        if (stat(path, &info) != 0 || !S_ISREG(info.st_mode)) continue;
        putf(page, "<li><a href='/edit?name=%s'>%s</a> (%lu bytes) <a href='/file/%s'>download</a> "
                   "<form method=post action=/files/delete><input type=hidden name=name value='%s'><button>Delete</button></form></li>",
             entry->d_name, entry->d_name, (unsigned long)info.st_size, entry->d_name, entry->d_name);
    }
    if (directory != NULL) closedir(directory);
    put(page, "</ul><p>Upload from a PC: curl -H 'Cookie: pim=PIN' -T file http://DEVICE:8080/upload/NAME</p>");
    finish_page(page, response);
}

static size_t read_file(const char *path, char *buffer, size_t size, bool *too_big) {
    FILE *file = fopen(path, "rb");
    size_t length;
    *too_big = false;
    if (file == NULL) return 0;
    length = fread(buffer, 1, size, file);
    *too_big = fgetc(file) != EOF;
    fclose(file);
    return length;
}

static void page_edit(pim_web_t *web, page_t *page, pim_web_response_t *response, const char *query, char *out, size_t out_size) {
    char name[64], path[160], text[PIM_WEB_EDIT_MAX + 1];
    size_t length;
    bool too_big;
    param_get(query, "name", name, sizeof(name));
    if (!name_ok(name)) { fail(response, out, out_size, 400, "Invalid file name"); return; }
    join_path(path, sizeof(path), web->root, name);
    length = read_file(path, text, PIM_WEB_EDIT_MAX, &too_big);
    if (too_big) { fail(response, out, out_size, 413, "File is too large to edit in the browser; download it instead"); return; }
    text[length] = '\0';
    page_begin(page, name);
    put(page, "<form method=post action=/save><input type=hidden name=name value='");
    put_escaped(page, name);
    put(page, "'><textarea name=text rows=22 spellcheck=false>");
    put_escaped(page, text);
    put(page, "</textarea><br><button>Save</button></form>");
    finish_page(page, response);
}

static bool write_file(pim_web_t *web, const char *name, const char *data, size_t length) {
    char path[160];
    FILE *file;
    bool ok;
    join_path(path, sizeof(path), web->root, name);
    file = fopen(path, "wb");
    if (file == NULL) return false;
    ok = length == 0 || fwrite(data, 1, length, file) == length;
    return fclose(file) == 0 && ok;
}

static void page_import(page_t *page, pim_web_response_t *response) {
    page_begin(page, "Calendar sync");
    put(page, "<p><a href=/calendar.ics>Download calendar and to-dos (.ics)</a></p>"
              "<p>Import an .ics file by pasting it here (times are taken as local time; duplicates are skipped):</p>"
              "<form method=post action=/import><textarea name=ics rows=12></textarea><br><button>Import</button></form>"
              "<p>From a PC: curl -H 'Cookie: pim=PIN' --data-binary @cal.ics http://DEVICE:8080/import</p>");
    finish_page(page, response);
}

static void do_import(pim_web_t *web, const char *body, size_t body_length, pim_web_response_t *response, char *out, size_t out_size) {
    static char text[PIM_WEB_BODY_MAX + 1];
    pim_ics_result_t result;
    page_t page = {out, out_size, 0, false};
    if (strncmp(body, "ics=", 4) == 0) param_get(body, "ics", text, sizeof(text));
    else copy_text(text, sizeof(text), body);
    (void)body_length;
    result = pim_ics_import(web->database, text);
    if (result.events + result.todos > 0) commit(web);
    out[0] = '\0';
    page_begin(&page, "Import finished");
    putf(&page, "<p>%u events and %u to-dos imported, %u duplicates skipped, %u rejected.</p>"
                "<p>The database holds up to %d appointments and %d to-dos.</p>",
         (unsigned)result.events, (unsigned)result.todos, (unsigned)result.skipped, (unsigned)result.rejected,
         PIM_MAX_APPOINTMENTS, PIM_MAX_TODOS);
    finish_page(&page, response);
}

bool pim_web_authorized(const pim_web_t *web, const char *cookie) {
    char expected[16];
    const char *found;
    if (cookie == NULL || web->pin[0] == '\0' || web->failures >= PIM_WEB_MAX_FAILURES) return false;
    snprintf(expected, sizeof(expected), "pim=%s", web->pin);
    for (found = cookie; (found = strstr(found, expected)) != NULL; found++)
        if ((found == cookie || found[-1] == ' ' || found[-1] == ';') && (found[strlen(expected)] == '\0' || found[strlen(expected)] == ';')) return true;
    return false;
}

void pim_web_handle(pim_web_t *web, const char *method, const char *target, const char *cookie,
                    const char *body, size_t body_length, char *out, size_t out_size,
                    pim_web_response_t *response) {
    char path[96];
    const char *query = strchr(target, '?');
    page_t page = {out, out_size, 0, false};
    bool post = strcmp(method, "POST") == 0, get = strcmp(method, "GET") == 0;
    size_t path_length = query == NULL ? strlen(target) : (size_t)(query - target);
    memset(response, 0, sizeof(*response));
    out[0] = '\0';
    if (path_length >= sizeof(path)) { fail(response, out, out_size, 400, "Path too long"); return; }
    memcpy(path, target, path_length);
    path[path_length] = '\0';
    query = query == NULL ? "" : query + 1;
    if (!get && !post) { fail(response, out, out_size, 400, "Unsupported method"); return; }

    if (post && strcmp(path, "/login") == 0) {
        char pin[16];
        param_get(body, "pin", pin, sizeof(pin));
        if (web->failures < PIM_WEB_MAX_FAILURES && web->pin[0] != '\0' && strcmp(pin, web->pin) == 0) {
            web->failures = 0;
            response->set_cookie = true;
            redirect(response, "/");
        } else {
            web->failures++;
            page_login(&page, response, web->failures >= PIM_WEB_MAX_FAILURES);
            response->status = 401;
        }
        return;
    }
    if (!pim_web_authorized(web, cookie)) {
        if (get && strncmp(path, "/file/", 6) != 0 && strcmp(path, "/calendar.ics") != 0) {
            page_login(&page, response, web->failures >= PIM_WEB_MAX_FAILURES);
            response->status = 401;
        } else fail(response, out, out_size, 401, "Log in first (send Cookie: pim=PIN)");
        return;
    }

    if (get && strcmp(path, "/") == 0) page_home(web, &page, response);
    else if (get && strcmp(path, "/agenda") == 0) page_agenda(web, &page, response, query);
    else if (post && strcmp(path, "/agenda/add") == 0) do_agenda_add(web, body, response, out, out_size);
    else if (post && strcmp(path, "/agenda/delete") == 0) do_agenda_delete(web, body, response);
    else if (get && strcmp(path, "/todos") == 0) page_todos(web, &page, response);
    else if (post && strncmp(path, "/todos/", 7) == 0 &&
             (strcmp(path + 7, "add") == 0 || strcmp(path + 7, "toggle") == 0 || strcmp(path + 7, "delete") == 0)) do_todo(web, path, body, response, out, out_size);
    else if (get && strcmp(path, "/notes") == 0) page_notes(web, &page, response);
    else if (get && strcmp(path, "/note") == 0) page_note(web, &page, response, query);
    else if (post && (strcmp(path, "/note/save") == 0 || strcmp(path, "/note/delete") == 0)) do_note(web, path, body, response, out, out_size);
    else if (get && strcmp(path, "/journal") == 0) page_journal(web, &page, response);
    else if (get && strcmp(path, "/journal/edit") == 0) page_journal_edit(web, &page, response, query);
    else if (post && (strcmp(path, "/journal/save") == 0 || strcmp(path, "/journal/delete") == 0)) do_journal(web, path, body, response, out, out_size);
    else if (get && strcmp(path, "/files") == 0) page_files(web, &page, response);
    else if (get && strcmp(path, "/edit") == 0) page_edit(web, &page, response, query, out, out_size);
    else if (post && strcmp(path, "/save") == 0) {
        char name[64];
        static char text[PIM_WEB_BODY_MAX + 1];
        param_get(body, "name", name, sizeof(name));
        param_get(body, "text", text, sizeof(text));
        if (!name_ok(name)) fail(response, out, out_size, 400, "Invalid file name");
        else if (strlen(text) > PIM_WEB_EDIT_MAX) fail(response, out, out_size, 413, "Text is limited to 4096 bytes");
        else if (!write_file(web, name, text, strlen(text))) fail(response, out, out_size, 500, "Could not write the file");
        else { web->changed = true; redirect(response, "/files"); }
    } else if (post && strcmp(path, "/files/delete") == 0) {
        char name[64], file_path[160];
        param_get(body, "name", name, sizeof(name));
        if (!name_ok(name)) { fail(response, out, out_size, 400, "Invalid file name"); return; }
        join_path(file_path, sizeof(file_path), web->root, name);
        remove(file_path);
        web->changed = true;
        redirect(response, "/files");
    } else if (get && strncmp(path, "/file/", 6) == 0) {
        char file_path[160];
        bool too_big;
        size_t length;
        if (!name_ok(path + 6)) { fail(response, out, out_size, 400, "Invalid file name"); return; }
        join_path(file_path, sizeof(file_path), web->root, path + 6);
        length = read_file(file_path, out, out_size - 1, &too_big);
        if (too_big) { fail(response, out, out_size, 413, "File is too large to download here"); return; }
        if (length == 0 && access(file_path, 0) != 0) { fail(response, out, out_size, 404, "File not found"); return; }
        out[length] = '\0';
        set_response(response, 200, "application/octet-stream", length);
    } else if (post && strncmp(path, "/upload/", 8) == 0) {
        if (!name_ok(path + 8)) fail(response, out, out_size, 400, "Invalid file name");
        else if (body_length > PIM_WEB_UPLOAD_MAX) fail(response, out, out_size, 413, "Uploads are limited to 8 KiB");
        else if (!write_file(web, path + 8, body, body_length)) fail(response, out, out_size, 500, "Could not write the file");
        else { web->changed = true; fail(response, out, out_size, 201, "Uploaded"); }
    } else if (get && strcmp(path, "/calendar.ics") == 0) {
        size_t length = pim_ics_export(web->database, out, out_size);
        if (length == 0) fail(response, out, out_size, 500, "Calendar too large to export");
        else set_response(response, 200, "text/calendar", length);
    } else if (get && strcmp(path, "/import") == 0) page_import(&page, response);
    else if (post && strcmp(path, "/import") == 0) do_import(web, body, body_length, response, out, out_size);
    else fail(response, out, out_size, 404, "Not found");
}
