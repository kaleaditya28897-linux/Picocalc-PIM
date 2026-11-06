#ifndef PIM_STORAGE_H
#define PIM_STORAGE_H

#include <stdbool.h>
#include "pim/pim.h"

bool pim_storage_load(const char *path, pim_database_t *database);
bool pim_storage_save(const char *path, const pim_database_t *database);
bool pim_storage_export(const char *root, const pim_database_t *database);

#endif
