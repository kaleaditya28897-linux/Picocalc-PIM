/* The startup report: what the device says after a start that did not finish.

   The registers it reads are hardware, so this test supplies the reading side
   and checks the part that has to be right when it matters - the one time
   anybody sees it, after a device has just stranded itself. */

#include "pim/boottrace.h"

#include <stdio.h>
#include <string.h>

static int checks, failures;

#define CHECK(expr) do { \
    checks++; \
    if (!(expr)) { printf("FAIL line %d: %s\n", __LINE__, #expr); failures++; } \
} while (0)

/* Stands in for the watchdog scratch registers; strong, so it replaces the
   weak definition in src/core/boottrace.c. */
static bool have;
static pim_boot_stage_t last_stage;
static pim_fault_t last_kind;
static unsigned last_detail;

bool pim_boot_last_failure(pim_boot_stage_t *stage, pim_fault_t *kind, unsigned *detail) {
    if (!have) return false;
    if (stage != NULL) *stage = last_stage;
    if (kind != NULL) *kind = last_kind;
    if (detail != NULL) *detail = last_detail;
    return true;
}

static const char *report(pim_boot_stage_t stage, pim_fault_t kind, unsigned detail) {
    static char out[96];
    have = true;
    last_stage = stage;
    last_kind = kind;
    last_detail = detail;
    pim_boot_failure_text(out, sizeof(out));
    return out;
}

static unsigned pack(const char *name) {
    unsigned packed = 0;
    int i;
    for (i = 0; i < 4 && name[i] != '\0'; i++) packed |= (unsigned)(unsigned char)name[i] << (8 * i);
    return packed;
}

int main(void) {
    char out[96];

    /* A start that finished says nothing at all: the report is only for the
       case where something went wrong. */
    have = false;
    pim_boot_failure_text(out, sizeof(out));
    CHECK(out[0] == '\0');

    /* The stage is the useful half - it says which part of startup to look at. */
    CHECK(strcmp(report(PIM_BOOT_WIFI_JOIN, PIM_FAULT_WATCHDOG, 0),
                 "Wi-Fi join stopped: stopped responding") == 0);
    CHECK(strcmp(report(PIM_BOOT_CARD, PIM_FAULT_HARD, 0),
                 "SD card stopped: hard fault") == 0);
    /* The stack fill pattern is not an address; it is an overflow. */
    CHECK(strcmp(report(PIM_BOOT_RUNNING, PIM_FAULT_HARD, 0xa5a5a5a5u),
                 "running stopped: stack overflow (ran off the end)") == 0);
    /* With an address, which is what makes a fault findable. */
    CHECK(strcmp(report(PIM_BOOT_CARD, PIM_FAULT_HARD, 0x10021a34),
                 "SD card stopped: hard fault at 0x10021a34") == 0);
    CHECK(strcmp(report(PIM_BOOT_TS_START, PIM_FAULT_ASSERT, 912),
                 "Tailscale start stopped: failed check (912)") == 0);

    /* A stack overflow names the task whose stack it was, which is the whole
       point of carrying four bytes through a reboot. */
    CHECK(strcmp(report(PIM_BOOT_WIFI_SCAN, PIM_FAULT_STACK, pack("ui")),
                 "Wi-Fi scan stopped: stack overflow in ui") == 0);
    CHECK(strcmp(report(PIM_BOOT_RUNNING, PIM_FAULT_STACK, pack("tcpip")),
                 "running stopped: stack overflow in tcpi") == 0);   /* four characters */
    CHECK(strcmp(report(PIM_BOOT_WIFI_INIT, PIM_FAULT_STACK, 0),
                 "Wi-Fi start stopped: stack overflow in ") == 0);   /* no name recorded */

    /* Out of memory reports what was left, in the units a person reads. */
    CHECK(strcmp(report(PIM_BOOT_TS_RUN, PIM_FAULT_MEMORY, 53 * 1024),
                 "Tailscale stopped: out of memory, 53 KB free") == 0);
    CHECK(strcmp(report(PIM_BOOT_TS_RUN, PIM_FAULT_MEMORY, 0),
                 "Tailscale stopped: out of memory, 0 KB free") == 0);

    /* Every stage and every fault has wording; none falls through to a number. */
    {
        int stage;
        for (stage = PIM_BOOT_START; stage <= PIM_BOOT_TS_RUN; stage++) {
            const char *text = pim_boot_stage_text((pim_boot_stage_t)stage);
            CHECK(text != NULL && text[0] != '\0' && strcmp(text, "unknown") != 0);
        }
        for (stage = PIM_FAULT_ASSERT; stage <= PIM_FAULT_WATCHDOG; stage++) {
            const char *text = pim_boot_fault_text((pim_fault_t)stage);
            CHECK(text != NULL && text[0] != '\0' && strcmp(text, "unknown") != 0);
        }
    }

    /* A buffer too small truncates rather than writing past the end. */
    {
        char small[12];
        have = true;
        last_stage = PIM_BOOT_WIFI_JOIN;
        last_kind = PIM_FAULT_WATCHDOG;
        last_detail = 0;
        memset(small, 0x7f, sizeof(small));
        pim_boot_failure_text(small, sizeof(small));
        CHECK(small[sizeof(small) - 1] == '\0' || strlen(small) < sizeof(small));
        CHECK(strlen(small) == sizeof(small) - 1);
    }

    printf("%d checks, %d failures\n", checks, failures);
    if (failures == 0) printf("startup report tests passed\n");
    return failures == 0 ? 0 : 1;
}
