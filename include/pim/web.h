#ifndef PIM_WEB_H
#define PIM_WEB_H

#include <stdbool.h>
#include <stddef.h>

#include "pim/pim.h"

#define PIM_WEB_BODY_MAX 12288
#define PIM_WEB_UPLOAD_MAX 8192
#define PIM_WEB_EDIT_MAX 4096
#define PIM_WEB_MAX_FAILURES 10

typedef struct {
    pim_database_t *database;
    const char *root;         /* directory holding user files, e.g. /PIM */
    const char *storage_path; /* snapshot path, or NULL to skip persistence */
    char pin[8];              /* shown on the device; browsers log in with it */
    pim_date_t today;
    uint16_t minutes;
    unsigned failures;
    bool changed;             /* set when the web client modified the database or files */
} pim_web_t;

typedef struct {
    int status;
    const char *type;
    size_t length;
    char location[96];
    bool set_cookie;
} pim_web_response_t;

/* Hardware-free HTTP router. `body` must be NUL-terminated; `body_length` is its real size. */
void pim_web_handle(pim_web_t *web, const char *method, const char *target, const char *cookie,
                    const char *body, size_t body_length, char *out, size_t out_size,
                    pim_web_response_t *response);
bool pim_web_authorized(const pim_web_t *web, const char *cookie);

#endif
