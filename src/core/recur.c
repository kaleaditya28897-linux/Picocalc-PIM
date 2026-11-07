#include "pim/tools.h"

#include <stdio.h>
#include <string.h>

int pim_day_number(pim_date_t date) {
    int days = 0;
    for (uint16_t year = 2000; year < date.year; year++) days += (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 366 : 365;
    for (uint8_t month = 1; month < date.month; month++) days += pim_days_in_month(date.year, month);
    return days + date.day - 1;
}

const char *pim_recur_name(char freq) {
    switch (freq) {
        case PIM_RECUR_DAILY: return "Daily";
        case PIM_RECUR_WEEKLY: return "Weekly";
        case PIM_RECUR_MONTHLY: return "Monthly";
        case PIM_RECUR_YEARLY: return "Yearly";
        default: return "?";
    }
}

bool pim_recur_occurs(const pim_recur_t *item, pim_date_t date) {
    uint8_t last;
    if (!pim_date_is_valid(date) || pim_date_compare(date, item->start) < 0) return false;
    switch (item->freq) {
        case PIM_RECUR_DAILY: return true;
        case PIM_RECUR_WEEKLY: return (pim_day_number(date) - pim_day_number(item->start)) % 7 == 0;
        case PIM_RECUR_MONTHLY:
            last = pim_days_in_month(date.year, date.month);
            return date.day == (item->start.day > last ? last : item->start.day);
        case PIM_RECUR_YEARLY:
            if (date.month != item->start.month) return false;
            last = pim_days_in_month(date.year, date.month);
            return date.day == (item->start.day > last ? last : item->start.day);
        default: return false;
    }
}

static bool freq_valid(char freq) {
    return freq == PIM_RECUR_DAILY || freq == PIM_RECUR_WEEKLY || freq == PIM_RECUR_MONTHLY || freq == PIM_RECUR_YEARLY;
}

size_t pim_recur_load(const char *root, pim_recur_t *list, size_t max) {
    char path[128], line[96];
    FILE *file;
    size_t count = 0;
    if (snprintf(path, sizeof(path), "%s/recurring.cfg", root) >= (int)sizeof(path)) return 0;
    file = fopen(path, "rb");
    if (file == NULL) return 0;
    while (count < max && fgets(line, sizeof(line), file) != NULL) {
        unsigned year, month, day, hour, minute;
        char freq;
        int offset = 0;
        pim_recur_t item;
        if (sscanf(line, "%4u-%2u-%2u %2u:%2u %c %n", &year, &month, &day, &hour, &minute, &freq, &offset) < 6 || offset == 0) continue;
        item.start = (pim_date_t){(uint16_t)year, (uint8_t)month, (uint8_t)day};
        if (!pim_date_is_valid(item.start) || hour > 23 || minute > 59 || !freq_valid(freq)) continue;
        item.minutes = (uint16_t)(hour * 60 + minute);
        item.freq = freq;
        snprintf(item.title, sizeof(item.title), "%s", line + offset);
        item.title[strcspn(item.title, "\r\n")] = '\0';
        if (item.title[0] == '\0') continue;
        list[count++] = item;
    }
    fclose(file);
    return count;
}

bool pim_recur_save(const char *root, const pim_recur_t *list, size_t count) {
    char path[128], temporary[136];
    FILE *file;
    bool ok = true;
    if (snprintf(path, sizeof(path), "%s/recurring.cfg", root) >= (int)sizeof(path)) return false;
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    file = fopen(temporary, "wb");
    if (file == NULL) return false;
    for (size_t i = 0; i < count && ok; i++)
        ok = fprintf(file, "%04u-%02u-%02u %02u:%02u %c %s\n", list[i].start.year, list[i].start.month, list[i].start.day,
                     list[i].minutes / 60, list[i].minutes % 60, list[i].freq, list[i].title) > 0;
    if (fclose(file) != 0) ok = false;
    if (!ok || rename(temporary, path) != 0) { remove(temporary); return false; }
    return true;
}

void pim_clock_format(uint32_t milliseconds, bool tenths, char *out, size_t size) {
    uint32_t total = milliseconds / 1000U;
    uint32_t hours = total / 3600U, minutes = (total / 60U) % 60U, seconds = total % 60U;
    if (hours > 0) snprintf(out, size, "%lu:%02lu:%02lu", (unsigned long)hours, (unsigned long)minutes, (unsigned long)seconds);
    else if (tenths) snprintf(out, size, "%02lu:%02lu.%lu", (unsigned long)minutes, (unsigned long)seconds, (unsigned long)((milliseconds / 100U) % 10U));
    else snprintf(out, size, "%02lu:%02lu", (unsigned long)minutes, (unsigned long)seconds);
}
