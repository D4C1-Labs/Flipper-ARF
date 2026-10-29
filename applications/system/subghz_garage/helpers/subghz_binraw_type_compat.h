#pragma once

#include <lib/subghz/types.h>

#ifdef SUBGHZ_GARAGE_HAS_BINRAW_TYPE

#define SUBGHZ_GARAGE_TYPE_BIN_RAW SubGhzProtocolTypeBinRAW

#else

#define SUBGHZ_GARAGE_TYPE_BIN_RAW ((SubGhzProtocolType)6)

#endif
