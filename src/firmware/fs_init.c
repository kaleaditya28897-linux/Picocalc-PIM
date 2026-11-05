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

bool fs_init(void) {
    blockdevice_t *sd = blockdevice_sd_create(spi0, 19, 16, 18, 17, 24 * MHZ, false);
    filesystem_t *fat = filesystem_fat_create();
    if (sd == NULL || fat == NULL || fs_mount("/", fat, sd) != 0) {
        printf("SD mount failed. Prepare a FAT32 card with tools/prepare-sd.sh\n");
        return false;
    }
    if (mkdir("/PIM", 0777) != 0 && errno != EEXIST) {
        printf("Cannot create /PIM: %s\n", strerror(errno));
        return false;
    }
    return true;
}
