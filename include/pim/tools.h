#ifndef PIM_TOOLS_H
#define PIM_TOOLS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pim/pim.h"

/* ---- Scientific calculator ---- */
/* Evaluates + - * / % ^, parentheses, pi, e, ans and sin cos tan asin acos atan sqrt cbrt ln log exp abs floor ceil round.
 * Returns false and fills `error` on failure. Trigonometry uses degrees when `degrees` is set. */
bool pim_calc_eval(const char *expression, double previous, bool degrees, double *result, char *error, size_t error_size);
void pim_calc_format(double value, char *out, size_t size);

/* ---- Search ---- */
typedef enum { PIM_HIT_APPOINTMENT, PIM_HIT_TODO, PIM_HIT_NOTE, PIM_HIT_JOURNAL, PIM_HIT_FILE } pim_hit_kind_t;
typedef struct {
    pim_hit_kind_t kind;
    size_t index;      /* database index for appointments, to-dos and notes */
    pim_date_t date;   /* journal entries and appointments */
    char title[40];
    char path[96];     /* files and journal entries */
    char snippet[44];
} pim_hit_t;
/* Case-insensitive search of the database, journal entries and text files in the PIM folder and code samples.
 * About 512 KB of file data is scanned per call. Returns the number of hits stored (at most `max`). */
size_t pim_search(const char *root, const pim_database_t *database, const char *query, pim_hit_t *hits, size_t max);

/* ---- Backups ---- */
#define PIM_BACKUP_NAME 24
#define PIM_BACKUP_KEEP 5
/* Copies the PIM data (database, settings, journal, code, games) to root/backup/<name>; keeps the newest PIM_BACKUP_KEEP. */
bool pim_backup_create(const char *root, const char *name);
size_t pim_backup_list(const char *root, char names[][PIM_BACKUP_NAME], size_t max);
bool pim_backup_restore(const char *root, const char *name);
bool pim_remove_tree(const char *path);

/* ---- Recurring appointments (root/recurring.cfg) ---- */
#define PIM_RECUR_MAX 24
typedef enum { PIM_RECUR_DAILY = 'D', PIM_RECUR_WEEKLY = 'W', PIM_RECUR_MONTHLY = 'M', PIM_RECUR_YEARLY = 'Y' } pim_recur_freq_t;
typedef struct { pim_date_t start; uint16_t minutes; char freq; char title[40]; } pim_recur_t;
size_t pim_recur_load(const char *root, pim_recur_t *list, size_t max);
bool pim_recur_save(const char *root, const pim_recur_t *list, size_t count);
bool pim_recur_occurs(const pim_recur_t *item, pim_date_t date);
/* Whole days from 2000-01-01 (used for weekly rules and tests). */
int pim_day_number(pim_date_t date);
const char *pim_recur_name(char freq);

/* ---- Timers ---- */
void pim_clock_format(uint32_t milliseconds, bool tenths, char *out, size_t size);

#endif
