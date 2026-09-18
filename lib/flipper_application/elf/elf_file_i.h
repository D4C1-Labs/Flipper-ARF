#pragma once
#include "elf_file.h"
#include "elf_file_xip.h"
#include <m-dict.h>

#ifdef __cplusplus
extern "C" {
#endif

DICT_DEF2(AddressCache, int, M_DEFAULT_OPLIST, Elf32_Addr, M_DEFAULT_OPLIST) //-V1048

/**
 * Callable elf entry type
 */
typedef int32_t(entry_t)(void*);

typedef struct ELFSection ELFSection;

struct ELFSection {
    void* data; /**< RAM buffer (staging for XIP, or runtime for RAM sections) */
    Elf32_Addr exec_addr; /**< Runtime address: flash for XIP, same as (Elf32_Addr)data for RAM */
    Elf32_Word size;
    Elf32_Word sh_flags; /**< Cached ELF section header flags */
    Elf32_Off file_offset; /**< Offset in ELF file (for deferred loading) */
    Elf32_Word file_align; /**< Alignment requirement from section header */
    bool xip; /**< true if section lives in flash XIP region */

    size_t rel_count;
    Elf32_Off rel_offset;
    ELFSection* fast_rel;

    uint16_t sec_idx;
};

DICT_DEF2(ELFSectionDict, const char*, M_CSTR_OPLIST, ELFSection, M_POD_OPLIST)

struct ELFFile {
    size_t sections_count;
    off_t section_table;
    off_t section_table_strings;

    size_t symbol_count;
    off_t symbol_table;
    off_t symbol_table_strings;
    off_t entry;
    ELFSectionDict_t sections;

    AddressCache_t relocation_cache;
    AddressCache_t trampoline_cache;

    File* fd;
    const ElfApiInterface* api_interface;
    ELFDebugLinkInfo debug_link_info;

    XipRegion xip_region;
    bool xip_disabled; /**< When true, skip XIP setup (used for plugins) */
    bool xip_forced; /**< When true, always use XIP even if app fits in RAM */

    ELFSection* preinit_array;
    ELFSection* init_array;
    ELFSection* fini_array;

    bool init_array_called;
};

#ifdef __cplusplus
}
#endif