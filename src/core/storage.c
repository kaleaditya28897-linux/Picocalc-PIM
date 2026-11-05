#include "pim/storage.h"

#include <stdio.h>

#define PIM_STORAGE_MAGIC 0x50494D31U

typedef struct {
    uint32_t magic, size, crc;
} header_t;

static uint32_t checksum(const void *data, size_t length) {
    const unsigned char *bytes = data;
    uint32_t value = 0xffffffffU;
    for (size_t i = 0; i < length; i++) {
        value ^= bytes[i];
        for (unsigned int bit = 0; bit < 8; bit++)
            value = (value >> 1) ^ (0xedb88320U & (-(int32_t)(value & 1U)));
    }
    return ~value;
}

bool pim_storage_load(const char *path, pim_database_t *database) {
    header_t header;
    pim_database_t loaded;
    FILE *file = fopen(path, "rb");
    if (file == NULL) { pim_database_init(database); return false; }
    if (fread(&header, sizeof(header), 1, file) != 1 || header.magic != PIM_STORAGE_MAGIC ||
        header.size != sizeof(loaded) || fread(&loaded, sizeof(loaded), 1, file) != 1 ||
        fgetc(file) != EOF || header.crc != checksum(&loaded, sizeof(loaded))) {
        fclose(file);
        pim_database_init(database);
        return false;
    }
    fclose(file);
    *database = loaded;
    return true;
}

bool pim_storage_save(const char *path, const pim_database_t *database) {
    char temporary[128];
    header_t header = {PIM_STORAGE_MAGIC, sizeof(*database), checksum(database, sizeof(*database))};
    int result = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    if (result < 0 || (size_t)result >= sizeof(temporary)) return false;
    FILE *file = fopen(temporary, "wb");
    if (file == NULL) return false;
    if (fwrite(&header, sizeof(header), 1, file) != 1 ||
        fwrite(database, sizeof(*database), 1, file) != 1 || fclose(file) != 0) {
        remove(temporary);
        return false;
    }
    if (rename(temporary, path) != 0) { remove(temporary); return false; }
    return true;
}
