#pragma once

#include <stdint.h>

typedef enum {
    RenaultProtoUnknown,
    RenaultProtoHitag,
    RenaultProtoMarelli,
    RenaultProtoValeo,
    RenaultProtoSiemens
} RenaultProtocolType;

RenaultProtocolType renault_classify(uint8_t bits);
