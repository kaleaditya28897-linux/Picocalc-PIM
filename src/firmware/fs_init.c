#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "blockdevice/sd.h"
#include "filesystem/fat.h"
#include "filesystem/vfs.h"
#include "hardware/clocks.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

/* Bring-up instrumentation: this is the one part of startup that has faulted
   on real hardware, and the fault is inside a library call, so the only way to
   say which is to announce each one before making it. */
#if PIM_TAILSCALE
#define CARD_TRACE(...) printf(__VA_ARGS__)
#else
#define CARD_TRACE(...) ((void)0)
#endif

bool fs_init(void) {
    blockdevice_t *sd = NULL;
    filesystem_t *fat = NULL;
    bool mounted = false;
    /* A card that has not been power-cycled can refuse the first initialisation
       after a warm reset - it is still in the state the last run left it in.
       Retrying costs a moment and turns "no card" into a working one. */
    for (int attempt = 0; attempt < 3 && !mounted; attempt++) {
        int rc;
        if (attempt > 0) busy_wait_ms(250);   /* never the scheduler's timer: this can run early */
        CARD_TRACE("card: creating block device\n");
        sd = blockdevice_sd_create(spi0, 19, 16, 18, 17, 24 * MHZ, false);
        CARD_TRACE("card: block device %s\n", sd != NULL ? "ok" : "NULL");
        fat = filesystem_fat_create();
        CARD_TRACE("card: fat %s\n", fat != NULL ? "ok" : "NULL");
        if (sd == NULL || fat == NULL) break;
        CARD_TRACE("card: mounting\n");
        rc = fs_mount("/", fat, sd);
        CARD_TRACE("card: mount returned %d\n", rc);
        if (rc == 0) { mounted = true; break; }
        if (fat != NULL) filesystem_fat_free(fat);
        if (sd != NULL) blockdevice_sd_free(sd);
        sd = NULL;
        fat = NULL;
    }
    if (!mounted) {
        printf("SD mount failed. Prepare a FAT32 card with tools/prepare-sd.sh\n");
        return false;
    }
    CARD_TRACE("card: mounted, making directories\n");
    if (mkdir("/PIM", 0777) != 0 && errno != EEXIST) {
        printf("Cannot create /PIM: %s\n", strerror(errno));
        return false;
    }
    static const char *const directories[] = {
        "/PIM/appointments", "/PIM/calendar", "/PIM/journal", "/PIM/notes", "/PIM/todos"
    };
    for (size_t i = 0; i < sizeof(directories) / sizeof(directories[0]); i++) {
        if (mkdir(directories[i], 0777) != 0 && errno != EEXIST) {
            printf("Cannot create %s: %s\n", directories[i], strerror(errno));
            return false;
        }
    }
    CARD_TRACE("card: ready\n");
    return true;
}
