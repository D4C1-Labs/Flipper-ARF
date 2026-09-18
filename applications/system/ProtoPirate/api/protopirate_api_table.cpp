#include <flipper_application/api_hashtable/api_hashtable.h>
#include <flipper_application/api_hashtable/compilesort.hpp>

/*
 * Private API table for ProtoPirate. Exposes the SubGhz library symbols (and
 * CC1101 presets) that ProtoPirate's runtime-loaded plugins import. The symbol
 * name `subghz_application_api_interface` matches what the vendored SubGhz
 * device registry (lib/subghz/devices/registry.c) expects, so the CC1101 ext
 * radio plugin resolves its presets through the same composite resolver.
 */
#include "protopirate_api_table_i.h"

static_assert(!has_hash_collisions(subghz_app_api_table), "Detected API method hash collision!");

constexpr HashtableApiInterface subghz_application_hashtable_api_interface{
    {
        .api_version_major = 0,
        .api_version_minor = 0,
        .resolver_callback = &elf_resolve_from_hashtable,
    },
    subghz_app_api_table.cbegin(),
    subghz_app_api_table.cend(),
};

extern "C" const ElfApiInterface* const subghz_application_api_interface =
    &subghz_application_hashtable_api_interface;
