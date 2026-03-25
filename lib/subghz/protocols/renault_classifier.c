#include "renault_classifier.h"
#include <stdint.h>

RenaultProtocolType renault_classify(uint8_t bits){

    if(bits >= 40 && bits <= 72)
        return RenaultProtoHitag;

    if(bits >= 90 && bits <= 110)
        return RenaultProtoMarelli;

    if(bits >= 64 && bits <= 80)
        return RenaultProtoSiemens;

    if(bits > 80 && bits <= 96)
        return RenaultProtoValeo;

    return RenaultProtoUnknown;
}
