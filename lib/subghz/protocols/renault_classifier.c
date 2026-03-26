#include "renault_classifier.h"
#include <stdint.h>

//   Hitag:   40–63  bits
//   Siemens: 64–80  bits
//   Valeo:   81–96  bits
//   Marelli: 97–110 bits
RenaultProtocolType renault_classify(uint8_t bits) {
    if(bits >= 40 && bits <= 63) return RenaultProtoHitag;
    if(bits >= 64 && bits <= 80) return RenaultProtoSiemens;
    if(bits >= 81 && bits <= 96) return RenaultProtoValeo;
    if(bits >= 97 && bits <= 110) return RenaultProtoMarelli;
    return RenaultProtoUnknown;
}
