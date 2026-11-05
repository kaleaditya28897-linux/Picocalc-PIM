#include "pim/vfs_compat.h"

#include <stdlib.h>
#include <string.h>

char *pim_vfs_strdup(const char *source) {
    size_t length = strlen(source) + 1;
    char *copy = malloc(length);
    if (copy != NULL) {
        memcpy(copy, source, length);
    }
    return copy;
}
