#pragma once

// [ARF_PORT] Use the app-local radio device loader copy (with si4463 support)
// instead of a lib/subghz one, so the loader code lives in the FAP and not in
// the firmware image.
#include "radio_device_loader.h"

#define subbrute_radio_device_loader_set radio_device_loader_set
#define subbrute_radio_device_loader_end radio_device_loader_end
