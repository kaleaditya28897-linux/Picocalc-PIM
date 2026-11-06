#include "pim/pim.h"

uint8_t pim_days_in_month(uint16_t year, uint8_t month) {
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 0;
    return month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)
               ? 29
               : days[month - 1];
}

bool pim_date_is_valid(pim_date_t date) {
    return date.year >= 2000 && date.month >= 1 && date.day >= 1 &&
           date.day <= pim_days_in_month(date.year, date.month);
}

int pim_date_compare(pim_date_t left, pim_date_t right) {
    if (left.year != right.year) return left.year < right.year ? -1 : 1;
    if (left.month != right.month) return left.month < right.month ? -1 : 1;
    if (left.day != right.day) return left.day < right.day ? -1 : 1;
    return 0;
}

uint8_t pim_first_day_of_month(uint16_t year, uint8_t month) {
    uint16_t century, year_of_century, day;
    if (month < 3) { month += 12; year--; }
    century = year / 100;
    year_of_century = year % 100;
    day = (1 + 13 * (month + 1) / 5 + year_of_century + year_of_century / 4 +
           century / 4 + 5 * century) % 7;
    return (uint8_t)((day + 5) % 7);
}

pim_date_t pim_date_shift(pim_date_t date, int days) {
    while (days > 0) {
        if (date.day < pim_days_in_month(date.year, date.month)) date.day++;
        else { date.day = 1; if (++date.month == 13) { date.month = 1; date.year++; } }
        days--;
    }
    while (days < 0) {
        if (date.day > 1) date.day--;
        else {
            if (date.month == 1 && date.year <= 2000) return date;
            if (--date.month == 0) { date.month = 12; date.year--; }
            date.day = pim_days_in_month(date.year, date.month);
        }
        days++;
    }
    return date;
}

pim_date_t pim_date_add_months(pim_date_t date, int months) {
    int index = (int)date.year * 12 + (date.month - 1) + months;
    uint8_t last;
    if (index < 2000 * 12) index = 2000 * 12;
    if (index > 2099 * 12 + 11) index = 2099 * 12 + 11;
    date.year = (uint16_t)(index / 12);
    date.month = (uint8_t)(index % 12 + 1);
    last = pim_days_in_month(date.year, date.month);
    if (date.day > last) date.day = last;
    return date;
}

uint16_t pim_entry_digit(uint16_t current, uint8_t *digits, unsigned digit, uint16_t max_value) {
    uint32_t extended = (uint32_t)current * 10U + digit;
    if (digit > 9) return current;
    if (*digits == 0 || extended > max_value) { *digits = 1; return (uint16_t)digit; }
    (*digits)++;
    return (uint16_t)extended;
}
