#ifndef PIM_LOCK_H
#define PIM_LOCK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A six-digit PIN asked for at startup, kept in /PIM/lock.cfg.

   What it is for: stopping someone who picks the device up from reading your
   calendar, your notes and your saved connections. What it is not for:
   protecting the SD card. The card is unencrypted and readable in any card
   reader, so the PIN is a lock on the front door, not a safe. The screens say
   as much, because a user who believes otherwise would make worse decisions.

   The stored form is a salted, iterated SHA-256 rather than the PIN itself, so
   someone reading the file cannot simply read the number off it. Six digits is
   only a million possibilities, so iteration is what buys the time: the count
   below costs about half a second on the device, which nobody notices once per
   boot, and turns an offline sweep of the whole space from seconds into hours. */

#define PIM_LOCK_DIGITS 6
/* Chosen for about half a second on a 150 MHz RP2350. */
#define PIM_LOCK_ROUNDS 50000u

/* True when the text is exactly six digits. */
bool pim_lock_pin_ok(const char *pin);

/* True when a PIN has been set. */
bool pim_lock_is_set(const char *path);

/* Stores a PIN, replacing any existing one. False when the PIN is not six
   digits, there is no entropy for a salt, or the card refuses the write. */
bool pim_lock_set(const char *path, const char *pin);

/* True when `pin` matches the stored one. False when nothing is stored, so a
   caller must check pim_lock_is_set() first to tell "no PIN" from "wrong PIN". */
bool pim_lock_check(const char *path, const char *pin);

/* Removes the PIN. True when the file went away. */
bool pim_lock_clear(const char *path);

#endif
