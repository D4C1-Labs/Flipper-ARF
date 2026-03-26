#pragma once

#include <stdint.h>

typedef enum {
    RenaultProtoUnknown,
    RenaultProtoHitag,
    RenaultProtoSiemens,
    RenaultProtoValeo,
    RenaultProtoMarelli,
} RenaultProtocolType;

RenaultProtocolType renault_classify(uint8_t bits);
