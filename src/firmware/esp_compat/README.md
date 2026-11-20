# ESP-IDF compatibility layer

`third_party/microlink` is written for ESP-IDF. Rather than edit 17,000 lines of
someone else's working code, this directory provides headers with the same names
and shapes as the ESP-IDF ones it includes, and `../ts_platform.c` implements
them against the Pico SDK, lwIP and FatFS.

The whole ESP-IDF surface the vendored subset touches is ten error codes, 26
functions and a handful of constants. Keeping it here means upstream stays
pristine and a new dependency shows up as a missing declaration at build time
rather than as a silent behaviour change.

Nothing outside the Tailscale build (`PIM_ENABLE_TAILSCALE=ON`) sees any of this.
