#ifndef PIM_TEXTFILE_H
#define PIM_TEXTFILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pim/pim.h"

/* Text files are edited one page (at most PIM_PAGE_BYTES - 1 bytes) at a time, so file size is limited only by the SD card. */
#define PIM_PAGE_BYTES 4096

bool pim_text_size(const char *path, uint32_t *size);
/* Reads the page starting at `offset`. Pages end at a line break when one exists; NUL bytes become spaces.
 * `length` is the number of file bytes consumed and `more` tells whether data follows. */
bool pim_text_page(const char *path, uint32_t offset, char *buffer, size_t capacity, size_t *length, bool *more);
/* Start of a line-aligned block of under PIM_PAGE_BYTES that ends at `offset`; approximate, so callers that page forward should remember offsets. */
uint32_t pim_text_previous(const char *path, uint32_t offset);
/* Replaces `old_length` bytes at `offset` with `data`, streaming through a temporary file. Creates the file if missing. */
/* errno of the last failed pim_text_replace, 0 if unknown. */
int pim_text_last_errno(void);
bool pim_text_replace(const char *path, uint32_t offset, uint32_t old_length, const char *data, size_t length);

/* Journal: one free-form text file per day, root/journal/DD-MM-YYYY.md. */
bool pim_journal_path(const char *root, pim_date_t date, char *out, size_t size);
bool pim_journal_exists(const char *root, pim_date_t date);
/* Newest-first list of at most `max` entry dates; returns how many were stored. `total` (optional) receives all entries. */
size_t pim_journal_list(const char *root, pim_date_t *dates, size_t max, size_t *total);
bool pim_journal_delete(const char *root, pim_date_t date);
/* Writes database journal entries to text files (when missing) and empties the table; true when the database changed. */
bool pim_journal_migrate(const char *root, pim_database_t *database);
/* First line of the entry without markup, for list previews. */
void pim_journal_preview(const char *root, pim_date_t date, char *out, size_t size);

#endif
