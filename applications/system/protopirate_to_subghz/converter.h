// converter.h - the conversion engine (name translation, brand routing,
// field copy, preset normalization, required-field check).
#pragma once

#include <furi.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    P2sResultOk, // file converted and written
    P2sResultSkipped, // intentionally not converted (already exists / not needed)
    P2sResultError, // read/write/required-field failure
} P2sResult;

// Convert one ProtoPirate .psf capture into a standard .sub file, routed to
// /ext/subghz/cars/<Brand>/<name>.sub. The Protocol field value is translated
// to the main-subghz registry name; the preset is normalized to the long form;
// per-protocol required fields are enforced.
P2sResult p2s_convert_psf_to_sub(const char* src_path);

// Convert one standard .sub file into a ProtoPirate .psf capture in
// /ext/apps_data/proto_pirate/saved/<name>.psf.
P2sResult p2s_convert_sub_to_psf(const char* src_path);

#ifdef __cplusplus
}
#endif
