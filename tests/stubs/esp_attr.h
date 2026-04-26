#pragma once

/* Host-test stub: IRAM_ATTR is a placement attribute meaningful only on
 * Xtensa / ESP32; on host builds it collapses to nothing. */
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

#ifndef DRAM_ATTR
#define DRAM_ATTR
#endif
