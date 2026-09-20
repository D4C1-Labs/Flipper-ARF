#include "elf_file.h"
#include "elf_file_i.h"
#include "elf_file_xip.h"

#include <storage/storage.h>
#include <elf.h>
#include <furi_hal_flash.h>
#include <toolbox/crc32_calc.h>
#include "elf_api_interface.h"
#include "../api_hashtable/api_hashtable.h"

#define TAG "Elf"

#define ELF_NAME_BUFFER_LEN        32
#define SECTION_OFFSET(e, n)       ((e)->section_table + (n) * sizeof(Elf32_Shdr))
#define IS_FLAGS_SET(v, m)         (((v) & (m)) == (m))
#define RESOLVER_THREAD_YIELD_STEP 30
#define FAST_RELOCATION_VERSION    1

// #define ELF_DEBUG_LOG 1

#ifndef ELF_DEBUG_LOG
#undef FURI_LOG_D
#define FURI_LOG_D(...)
#endif

#define ELF_INVALID_ADDRESS 0xFFFFFFFF

#define TRAMPOLINE_CODE_SIZE 6

/**
ldr r12, [pc, #2]
bx r12
*/
const uint8_t trampoline_code_little_endian[TRAMPOLINE_CODE_SIZE] =
    {0xdf, 0xf8, 0x02, 0xc0, 0x60, 0x47};

typedef struct {
    uint8_t code[TRAMPOLINE_CODE_SIZE];
    uint32_t addr;
} FURI_PACKED JMPTrampoline;

/**************************************************************************************************/
/********************************************* Caches *********************************************/
/**************************************************************************************************/

static bool address_cache_get(AddressCache_t cache, int symEntry, Elf32_Addr* symAddr) {
    Elf32_Addr* addr = AddressCache_get(cache, symEntry);
    if(addr) {
        *symAddr = *addr;
        return true;
    } else {
        return false;
    }
}

static void address_cache_put(AddressCache_t cache, int symEntry, Elf32_Addr symAddr) {
    AddressCache_set_at(cache, symEntry, symAddr);
}

/**************************************************************************************************/
/********************************************** ELF ***********************************************/
/**************************************************************************************************/

static void elf_file_maybe_release_fd(ELFFile* elf) {
    if(elf->fd) {
        storage_file_free(elf->fd);
        elf->fd = NULL;
    }
}

static ELFSection* elf_file_get_section(ELFFile* elf, const char* name) {
    return ELFSectionDict_get(elf->sections, name);
}

static ELFSection* elf_file_get_or_put_section(ELFFile* elf, const char* name) {
    ELFSection* section_p = elf_file_get_section(elf, name);
    if(!section_p) {
        ELFSectionDict_set_at(
            elf->sections,
            strdup(name),
            (ELFSection){
                .data = NULL,
                .exec_addr = 0,
                .sec_idx = 0,
                .size = 0,
                .sh_flags = 0,
                .file_offset = 0,
                .file_align = 0,
                .xip = false,
                .rel_count = 0,
                .rel_offset = 0,
                .fast_rel = NULL,
            });
        section_p = elf_file_get_section(elf, name);
    }

    return section_p;
}

static bool elf_read_string_from_offset(ELFFile* elf, off_t offset, FuriString* name) {
    bool result = false;

    off_t old = storage_file_tell(elf->fd);

    do {
        if(!storage_file_seek(elf->fd, offset, true)) break;

        char buffer[ELF_NAME_BUFFER_LEN + 1];
        buffer[ELF_NAME_BUFFER_LEN] = 0;

        while(true) {
            size_t read = storage_file_read(elf->fd, buffer, ELF_NAME_BUFFER_LEN);
            furi_string_cat(name, buffer);
            if(strlen(buffer) < ELF_NAME_BUFFER_LEN) {
                result = true;
                break;
            }

            if(storage_file_get_error(elf->fd) != FSE_OK || read == 0) break;
        }

    } while(false);
    storage_file_seek(elf->fd, old, true);

    return result;
}

static bool elf_read_section_name(ELFFile* elf, off_t offset, FuriString* name) {
    return elf_read_string_from_offset(elf, elf->section_table_strings + offset, name);
}

static bool elf_read_symbol_name(ELFFile* elf, off_t offset, FuriString* name) {
    return elf_read_string_from_offset(elf, elf->symbol_table_strings + offset, name);
}

static bool elf_read_section_header(ELFFile* elf, size_t section_idx, Elf32_Shdr* section_header) {
    off_t offset = SECTION_OFFSET(elf, section_idx);
    return storage_file_seek(elf->fd, offset, true) &&
           storage_file_read(elf->fd, section_header, sizeof(Elf32_Shdr)) == sizeof(Elf32_Shdr);
}

static bool elf_read_section(
    ELFFile* elf,
    size_t section_idx,
    Elf32_Shdr* section_header,
    FuriString* name) {
    if(!elf_read_section_header(elf, section_idx, section_header)) {
        return false;
    }

    if(section_header->sh_name && !elf_read_section_name(elf, section_header->sh_name, name)) {
        return false;
    }

    return true;
}

static bool elf_read_symbol(ELFFile* elf, int n, Elf32_Sym* sym, FuriString* name) {
    bool success = false;
    off_t old = storage_file_tell(elf->fd);
    off_t pos = elf->symbol_table + n * sizeof(Elf32_Sym);
    if(storage_file_seek(elf->fd, pos, true) &&
       storage_file_read(elf->fd, sym, sizeof(Elf32_Sym)) == sizeof(Elf32_Sym)) {
        if(sym->st_name)
            success = elf_read_symbol_name(elf, sym->st_name, name);
        else {
            Elf32_Shdr shdr;
            success = elf_read_section(elf, sym->st_shndx, &shdr, name);
        }
    }
    storage_file_seek(elf->fd, old, true);
    return success;
}

static ELFSection* elf_section_of(ELFFile* elf, int index) {
    ELFSectionDict_it_t it;
    for(ELFSectionDict_it(it, elf->sections); !ELFSectionDict_end_p(it); ELFSectionDict_next(it)) {
        ELFSectionDict_itref_t* itref = ELFSectionDict_ref(it);
        if(itref->value.sec_idx == index) {
            return &itref->value;
        }
    }

    return NULL;
}

static Elf32_Addr elf_address_of(ELFFile* elf, Elf32_Sym* sym, const char* sName) {
    if(sym->st_shndx == SHN_UNDEF) {
        Elf32_Addr addr = 0;
        uint32_t hash = elf_symbolname_hash(sName);
        if(elf->api_interface->resolver_callback(elf->api_interface, hash, &addr)) {
            return addr;
        }
    } else {
        ELFSection* symSec = elf_section_of(elf, sym->st_shndx);
        if(symSec) {
            return (symSec->exec_addr) + sym->st_value;
        }
    }
    FURI_LOG_D(TAG, "  Can not find address for symbol %s", sName);
    return ELF_INVALID_ADDRESS;
}

__attribute__((unused)) static const char* elf_reloc_type_to_str(int symt) {
#define STRCASE(name) \
    case name:        \
        return #name;
    switch(symt) {
        STRCASE(R_ARM_NONE)
        STRCASE(R_ARM_TARGET1)
        STRCASE(R_ARM_ABS32)
        STRCASE(R_ARM_REL32)
        STRCASE(R_ARM_THM_PC22)
        STRCASE(R_ARM_THM_JUMP24)
    default:
        return "R_<unknow>";
    }
#undef STRCASE
}

static JMPTrampoline* elf_create_trampoline(Elf32_Addr addr) {
    JMPTrampoline* trampoline = malloc(sizeof(JMPTrampoline));
    memcpy(trampoline->code, trampoline_code_little_endian, TRAMPOLINE_CODE_SIZE);
    trampoline->addr = addr;
    return trampoline;
}

static void elf_relocate_jmp_call(
    ELFFile* elf,
    Elf32_Addr patchAddr,
    Elf32_Addr relAddr,
    int type,
    Elf32_Addr symAddr) {
    int offset, hi, lo, s, j1, j2, i1, i2, imm10, imm11;
    int to_thumb, is_call, blx_bit = 1 << 12;

    /* Get initial offset (instruction bytes are read from RAM staging) */
    hi = ((uint16_t*)patchAddr)[0];
    lo = ((uint16_t*)patchAddr)[1];
    s = (hi >> 10) & 1;
    j1 = (lo >> 13) & 1;
    j2 = (lo >> 11) & 1;
    i1 = (j1 ^ s) ^ 1;
    i2 = (j2 ^ s) ^ 1;
    imm10 = hi & 0x3ff;
    imm11 = lo & 0x7ff;
    offset = (s << 24) | (i1 << 23) | (i2 << 22) | (imm10 << 12) | (imm11 << 1);
    if(offset & 0x01000000) offset -= 0x02000000;

    to_thumb = symAddr & 1;
    is_call = (type == R_ARM_THM_PC22);

    /* Store offset */
    int offset_copy = offset;

    /* Compute final offset */
    offset += symAddr - relAddr;
    if(!to_thumb && is_call) {
        blx_bit = 0; /* bl -> blx */
        offset = (offset + 3) & -4; /* Compute offset from aligned PC */
    }

    /* Check that relocation is possible
    * offset must not be out of range
    * if target is to be entered in arm mode:
        - bit 1 must not set
        - instruction must be a call (bl) or a jump to PLT */
    if(!to_thumb || offset >= 0x1000000 || offset < -0x1000000) {
        if(to_thumb || (symAddr & 2) || (!is_call)) {
            FURI_LOG_D(
                TAG,
                "can't relocate value at %lx, %s, doing trampoline",
                relAddr,
                elf_reloc_type_to_str(type));

            Elf32_Addr addr;
            if(!address_cache_get(elf->trampoline_cache, symAddr, &addr)) {
                addr = (Elf32_Addr)elf_create_trampoline(symAddr);
                address_cache_put(elf->trampoline_cache, symAddr, addr);
            }

            offset = offset_copy;
            offset += (int)addr - relAddr;
            if(!to_thumb && is_call) {
                blx_bit = 0; /* bl -> blx */
                offset = (offset + 3) & -4; /* Compute offset from aligned PC */
            }
        }
    }

    /* Compute and store final offset */
    s = (offset >> 24) & 1;
    i1 = (offset >> 23) & 1;
    i2 = (offset >> 22) & 1;
    j1 = s ^ (i1 ^ 1);
    j2 = s ^ (i2 ^ 1);
    imm10 = (offset >> 12) & 0x3ff;
    imm11 = (offset >> 1) & 0x7ff;
    (*(uint16_t*)patchAddr) = (uint16_t)((hi & 0xf800) | (s << 10) | imm10);
    (*(uint16_t*)(patchAddr + 2)) =
        (uint16_t)((lo & 0xc000) | (j1 << 13) | blx_bit | (j2 << 11) | imm11);
}

static void elf_relocate_mov(Elf32_Addr patchAddr, int type, Elf32_Addr symAddr) {
    uint16_t upper_insn = ((uint16_t*)patchAddr)[0];
    uint16_t lower_insn = ((uint16_t*)patchAddr)[1];

    /* MOV*<C> <Rd>,#<imm16>
     *
     * i = upper[10]
     * imm4 = upper[3:0]
     * imm3 = lower[14:12]
     * imm8 = lower[7:0]
     *
     * imm16 = imm4:i:imm3:imm8
     */
    uint32_t i = (upper_insn >> 10) & 1; /* upper[10] */
    uint32_t imm4 = upper_insn & 0x000F; /* upper[3:0] */
    uint32_t imm3 = (lower_insn >> 12) & 0x7; /* lower[14:12] */
    uint32_t imm8 = lower_insn & 0x00FF; /* lower[7:0] */

    int32_t addend = (imm4 << 12) | (i << 11) | (imm3 << 8) | imm8; /* imm16 */

    uint32_t addr = (symAddr + addend);
    if(type == R_ARM_THM_MOVT_ABS) {
        addr >>= 16; /* upper 16 bits */
    } else {
        addr &= 0x0000FFFF; /* lower 16 bits */
    }

    /* Re-encode */
    ((uint16_t*)patchAddr)[0] = (upper_insn & 0xFBF0) | (((addr >> 11) & 1) << 10) /* i */
                                | ((addr >> 12) & 0x000F); /* imm4 */
    ((uint16_t*)patchAddr)[1] = (lower_insn & 0x8F00) | (((addr >> 8) & 0x7) << 12) /* imm3 */
                                | (addr & 0x00FF); /* imm8 */
}

/**
 * @param patchAddr  RAM address where instruction/data bytes are read/written
 *                   (staging buffer for XIP sections, runtime addr otherwise)
 * @param relAddr    Runtime address used for PC-relative relocations
 *                   (flash exec address for XIP sections)
 */
static bool elf_relocate_symbol(
    ELFFile* elf,
    Elf32_Addr patchAddr,
    Elf32_Addr relAddr,
    int type,
    Elf32_Addr symAddr) {
    switch(type) {
    case R_ARM_TARGET1:
    case R_ARM_ABS32:
        *((uint32_t*)patchAddr) += symAddr;
        FURI_LOG_D(
            TAG, "  R_ARM_ABS32 relocated is 0x%08X", (unsigned int)*((uint32_t*)patchAddr));
        break;
    case R_ARM_REL32:
        *((uint32_t*)patchAddr) += symAddr - relAddr;
        FURI_LOG_D(
            TAG, "  R_ARM_REL32 relocated is 0x%08X", (unsigned int)*((uint32_t*)patchAddr));
        break;
    case R_ARM_THM_PC22:
    case R_ARM_CALL:
    case R_ARM_THM_JUMP24:
        elf_relocate_jmp_call(elf, patchAddr, relAddr, type, symAddr);
        FURI_LOG_D(
            TAG,
            "  R_ARM_THM_CALL/JMP relocated is 0x%08X",
            (unsigned int)*((uint32_t*)patchAddr));
        break;
    case R_ARM_THM_MOVW_ABS_NC:
    case R_ARM_THM_MOVT_ABS:
        elf_relocate_mov(patchAddr, type, symAddr);
        FURI_LOG_D(
            TAG,
            "  R_ARM_THM_MOVW_ABS_NC/MOVT_ABS relocated is 0x%08X",
            (unsigned int)*((uint32_t*)patchAddr));
        break;
    default:
        FURI_LOG_E(TAG, "  Undefined relocation %d", type);
        return false;
    }
    return true;
}

static bool elf_relocate(ELFFile* elf, ELFSection* s) {
    if(s->data) {
        Elf32_Rel rel;
        size_t relEntries = s->rel_count;
        size_t relCount;
        (void)storage_file_seek(elf->fd, s->rel_offset, true);
        FURI_LOG_D(TAG, " Offset   Info     Type             Name");

        int relocate_result = true;
        FuriString* symbol_name;
        symbol_name = furi_string_alloc();

        for(relCount = 0; relCount < relEntries; relCount++) {
            if(relCount % RESOLVER_THREAD_YIELD_STEP == 0) {
                FURI_LOG_D(TAG, "  reloc YIELD");
                furi_delay_tick(1);
            }

            if(storage_file_read(elf->fd, &rel, sizeof(Elf32_Rel)) != sizeof(Elf32_Rel)) {
                FURI_LOG_E(TAG, "  reloc read fail");
                furi_string_free(symbol_name);
                return false;
            }

            Elf32_Addr symAddr;

            int symEntry = ELF32_R_SYM(rel.r_info);
            int relType = ELF32_R_TYPE(rel.r_info);
            Elf32_Addr patchAddr = ((Elf32_Addr)s->data) + rel.r_offset;
            Elf32_Addr relAddr = (s->exec_addr) + rel.r_offset;

            if(!address_cache_get(elf->relocation_cache, symEntry, &symAddr)) {
                Elf32_Sym sym;
                furi_string_reset(symbol_name);
                if(!elf_read_symbol(elf, symEntry, &sym, symbol_name)) {
                    FURI_LOG_E(TAG, "  symbol read fail");
                    furi_string_free(symbol_name);
                    return false;
                }

                FURI_LOG_D(
                    TAG,
                    " %08X %08X %-16s %s",
                    (unsigned int)rel.r_offset,
                    (unsigned int)rel.r_info,
                    elf_reloc_type_to_str(relType),
                    furi_string_get_cstr(symbol_name));

                symAddr = elf_address_of(elf, &sym, furi_string_get_cstr(symbol_name));
                address_cache_put(elf->relocation_cache, symEntry, symAddr);
            }

            if(symAddr != ELF_INVALID_ADDRESS) {
                FURI_LOG_D(
                    TAG,
                    "  symAddr=%08X relAddr=%08X",
                    (unsigned int)symAddr,
                    (unsigned int)relAddr);
                if(!elf_relocate_symbol(elf, patchAddr, relAddr, relType, symAddr)) {
                    relocate_result = false;
                }
            } else {
                FURI_LOG_E(TAG, "  No symbol address of %s", furi_string_get_cstr(symbol_name));
                relocate_result = false;
            }
        }
        furi_string_free(symbol_name);

        return relocate_result;
    } else {
        FURI_LOG_D(TAG, "Section not loaded");
    }

    return false;
}

/**************************************************************************************************/
/************************************ Internal FAP interfaces *************************************/
/**************************************************************************************************/
typedef enum {
    SectionTypeUnused = 1 << 0,
    SectionTypeData = 1 << 1,
    SectionTypeRelData = 1 << 2,
    SectionTypeSymTab = 1 << 3,
    SectionTypeStrTab = 1 << 4,
    SectionTypeDebugLink = 1 << 5,
    SectionTypeFastRelData = 1 << 6,
} SectionType;

static bool elf_load_debug_link(ELFFile* elf, Elf32_Shdr* section_header) {
    elf->debug_link_info.debug_link_size = section_header->sh_size;
    elf->debug_link_info.debug_link = malloc(section_header->sh_size);

    return storage_file_seek(elf->fd, section_header->sh_offset, true) &&
           storage_file_read(elf->fd, elf->debug_link_info.debug_link, section_header->sh_size) ==
               section_header->sh_size;
}

static bool str_prefix(const char* str, const char* prefix) {
    return strncmp(prefix, str, strlen(prefix)) == 0;
}

typedef enum {
    ELFLoadSectionResultSuccess,
    ELFLoadSectionResultNoMemory,
    ELFLoadSectionResultError,
} ELFLoadSectionResult;

typedef struct {
    SectionType type;
    ELFLoadSectionResult result;
} SectionTypeInfo;

/** Save section metadata without allocating RAM or reading data.
 *  Actual loading is deferred to elf_materialize_section(), after we know
 *  which sections can live in the XIP flash region instead of RAM.
 *  BSS (SHT_NOBITS) is allocated eagerly since it's cheap and always needed.
 */
static ELFLoadSectionResult
    elf_save_section_metadata(ELFSection* section, Elf32_Shdr* section_header) {
    section->size = section_header->sh_size;
    section->file_offset = section_header->sh_offset;
    section->file_align = section_header->sh_addralign;
    section->data = NULL;

    if(section_header->sh_type == SHT_NOBITS) {
        /* BSS: allocate zeroed RAM immediately. */
        if(section_header->sh_size > 0) {
            section->data = aligned_malloc(section_header->sh_size, section_header->sh_addralign);
            if(!section->data) return ELFLoadSectionResultNoMemory;
            memset(section->data, 0, section_header->sh_size);
        }
    }

    return ELFLoadSectionResultSuccess;
}

/** Allocate RAM and read section data from the ELF file.
 *  Called after XIP setup so we know which sections need RAM.
 *  Handles sections > 64 KB via chunked reads.
 */
static ELFLoadSectionResult elf_materialize_section(ELFFile* elf, ELFSection* section) {
    if(section->size == 0 || section->data != NULL) {
        /* Already materialized (BSS) or empty. */
        return ELFLoadSectionResultSuccess;
    }

    size_t safe_size = section->size + 1024;

    furi_kernel_lock();

    if(memmgr_heap_get_max_free_block() < safe_size) {
        furi_kernel_unlock();
        FURI_LOG_E(TAG, "Not enough memory to load section data (%lu bytes)", section->size);
        return ELFLoadSectionResultNoMemory;
    }

    section->data = aligned_malloc(section->size, section->file_align);

    furi_kernel_unlock();

    if(!section->data) {
        return ELFLoadSectionResultNoMemory;
    }

    if(!storage_file_seek(elf->fd, section->file_offset, true)) {
        FURI_LOG_E(TAG, "    seek fail");
        aligned_free(section->data);
        section->data = NULL;
        return ELFLoadSectionResultError;
    }

    /* Read section data — chunk reads defensively (FatFS single-call limit). */
    size_t total_read = 0;
    while(total_read < section->size) {
        size_t chunk = section->size - total_read;
        if(chunk > 0xF000) chunk = 0xF000;
        size_t got = storage_file_read(elf->fd, (uint8_t*)section->data + total_read, chunk);
        if(got == 0) {
            FURI_LOG_E(TAG, "    read fail at %zu/%lu", total_read, section->size);
            aligned_free(section->data);
            section->data = NULL;
            return ELFLoadSectionResultError;
        }
        total_read += got;
    }

    FURI_LOG_D(TAG, "Materialized %lu bytes at 0x%p", section->size, section->data);
    return ELFLoadSectionResultSuccess;
}

static SectionTypeInfo elf_preload_section(
    ELFFile* elf,
    size_t section_idx,
    Elf32_Shdr* section_header,
    FuriString* name_string) {
    const char* name = furi_string_get_cstr(name_string);
    SectionTypeInfo info;

#ifdef ELF_DEBUG_LOG
    // log section name, type and flags
    FuriString* flags_string = furi_string_alloc();
    if(section_header->sh_flags & SHF_WRITE) furi_string_cat(flags_string, "W");
    if(section_header->sh_flags & SHF_ALLOC) furi_string_cat(flags_string, "A");
    if(section_header->sh_flags & SHF_EXECINSTR) furi_string_cat(flags_string, "X");
    if(section_header->sh_flags & SHF_MERGE) furi_string_cat(flags_string, "M");
    if(section_header->sh_flags & SHF_STRINGS) furi_string_cat(flags_string, "S");
    if(section_header->sh_flags & SHF_INFO_LINK) furi_string_cat(flags_string, "I");
    if(section_header->sh_flags & SHF_LINK_ORDER) furi_string_cat(flags_string, "L");
    if(section_header->sh_flags & SHF_OS_NONCONFORMING) furi_string_cat(flags_string, "O");
    if(section_header->sh_flags & SHF_GROUP) furi_string_cat(flags_string, "G");
    if(section_header->sh_flags & SHF_TLS) furi_string_cat(flags_string, "T");
    if(section_header->sh_flags & SHF_COMPRESSED) furi_string_cat(flags_string, "T");
    if(section_header->sh_flags & SHF_MASKOS) furi_string_cat(flags_string, "o");
    if(section_header->sh_flags & SHF_MASKPROC) furi_string_cat(flags_string, "p");
    if(section_header->sh_flags & SHF_ORDERED) furi_string_cat(flags_string, "R");
    if(section_header->sh_flags & SHF_EXCLUDE) furi_string_cat(flags_string, "E");

    FURI_LOG_I(
        TAG,
        "Section %s: type: %ld, flags: %s",
        name,
        section_header->sh_type,
        furi_string_get_cstr(flags_string));
    furi_string_free(flags_string);
#endif

    // ignore .ARM and .rel.ARM sections
    // TODO FL-3525: how to do it not by name?
    // .ARM: type 0x70000001, flags SHF_ALLOC | SHF_LINK_ORDER
    // .rel.ARM: type 0x9, flags SHT_REL
    if(str_prefix(name, ".ARM.") || str_prefix(name, ".rel.ARM.") ||
       str_prefix(name, ".fast.rel.ARM.")) {
        FURI_LOG_D(TAG, "Ignoring ARM section");

        info.type = SectionTypeUnused;
        info.result = ELFLoadSectionResultSuccess;
        return info;
    }

    // Load allocable section
    if(section_header->sh_flags & SHF_ALLOC) {
        ELFSection* section_p = elf_file_get_or_put_section(elf, name);
        section_p->sec_idx = section_idx;
        section_p->sh_flags = section_header->sh_flags;

        if(section_header->sh_type == SHT_PREINIT_ARRAY) {
            furi_assert(elf->preinit_array == NULL);
            elf->preinit_array = section_p;
        } else if(section_header->sh_type == SHT_INIT_ARRAY) {
            furi_assert(elf->init_array == NULL);
            elf->init_array = section_p;
        } else if(section_header->sh_type == SHT_FINI_ARRAY) {
            furi_assert(elf->fini_array == NULL);
            elf->fini_array = section_p;
        }

        info.type = SectionTypeData;
        /* Defer allocation/loading — XIP setup decides RAM vs flash later. */
        info.result = elf_save_section_metadata(section_p, section_header);

        if(info.result != ELFLoadSectionResultSuccess) {
            FURI_LOG_E(TAG, "Error saving metadata for section '%s'", name);
        }

        return info;
    }

    // Load link info section
    if(section_header->sh_flags & SHF_INFO_LINK) {
        info.type = SectionTypeRelData;

        if(str_prefix(name, ".rel")) {
            name = name + strlen(".rel");
            ELFSection* section_p = elf_file_get_or_put_section(elf, name);
            section_p->rel_count = section_header->sh_size / sizeof(Elf32_Rel);
            section_p->rel_offset = section_header->sh_offset;
            info.result = ELFLoadSectionResultSuccess;
        } else {
            FURI_LOG_E(TAG, "Unknown link info section '%s'", name);
            info.result = ELFLoadSectionResultError;
        }

        return info;
    }

    // Load fast rel section (small — load eagerly into RAM now)
    if(str_prefix(name, ".fast.rel")) {
        name = name + strlen(".fast.rel");
        ELFSection* section_p = elf_file_get_or_put_section(elf, name);
        section_p->fast_rel = malloc(sizeof(ELFSection));
        memset(section_p->fast_rel, 0, sizeof(ELFSection));

        info.type = SectionTypeFastRelData;
        info.result = elf_save_section_metadata(section_p->fast_rel, section_header);
        if(info.result == ELFLoadSectionResultSuccess) {
            info.result = elf_materialize_section(elf, section_p->fast_rel);
        }

        if(info.result != ELFLoadSectionResultSuccess) {
            FURI_LOG_E(TAG, "Error loading section '%s'", name);
        } else {
            FURI_LOG_D(TAG, "Loaded fast rel section for '%s'", name);
        }

        return info;
    }

    // Load symbol table
    if(strcmp(name, ".symtab") == 0) {
        FURI_LOG_D(TAG, "Found .symtab section");
        elf->symbol_table = section_header->sh_offset;
        elf->symbol_count = section_header->sh_size / sizeof(Elf32_Sym);

        info.type = SectionTypeSymTab;
        info.result = ELFLoadSectionResultSuccess;
        return info;
    }

    // Load string table
    if(strcmp(name, ".strtab") == 0) {
        FURI_LOG_D(TAG, "Found .strtab section");
        elf->symbol_table_strings = section_header->sh_offset;

        info.type = SectionTypeStrTab;
        info.result = ELFLoadSectionResultSuccess;
        return info;
    }

    // Load debug link section
    if(strcmp(name, ".gnu_debuglink") == 0) {
        FURI_LOG_D(TAG, "Found .gnu_debuglink section");
        info.type = SectionTypeDebugLink;

        if(elf_load_debug_link(elf, section_header)) {
            info.result = ELFLoadSectionResultSuccess;
            return info;
        } else {
            info.result = ELFLoadSectionResultError;
            return info;
        }
    }

    info.type = SectionTypeUnused;
    info.result = ELFLoadSectionResultSuccess;
    return info;
}

static Elf32_Addr elf_address_of_by_hash(ELFFile* elf, uint32_t hash) {
    Elf32_Addr addr = 0;
    if(elf->api_interface->resolver_callback(elf->api_interface, hash, &addr)) {
        return addr;
    }
    return ELF_INVALID_ADDRESS;
}

static bool elf_file_find_string_by_hash(ELFFile* elf, uint32_t hash, FuriString* out) {
    bool result = false;

    FuriString* symbol_name = furi_string_alloc();
    Elf32_Sym sym;
    for(size_t i = 0; i < elf->symbol_count; i++) {
        furi_string_reset(symbol_name);
        if(elf_read_symbol(elf, i, &sym, symbol_name)) {
            if(elf_symbolname_hash(furi_string_get_cstr(symbol_name)) == hash) {
                furi_string_set(out, symbol_name);
                result = true;
                break;
            }
        }
    }
    furi_string_free(symbol_name);

    return result;
}

static bool elf_relocate_fast(ELFFile* elf, ELFSection* s) {
    UNUSED(elf);
    const uint8_t* start = s->fast_rel->data;
    const uint8_t version = *start;
    bool no_errors = true;

    if(version != FAST_RELOCATION_VERSION) {
        FURI_LOG_E(TAG, "Unsupported fast relocation version %d", version);
        return false;
    }
    start += 1;

    const uint32_t records_count = *((uint32_t*)start);
    start += 4;
    FURI_LOG_D(TAG, "Fast relocation records count: %ld", records_count);

    for(uint32_t i = 0; i < records_count; i++) {
        bool is_section = (*start & (0x1 << 7)) ? true : false;
        uint8_t type = *start & 0x7F;
        start += 1;
        uint32_t hash_or_section_index = *((uint32_t*)start);
        start += 4;

        uint32_t section_value = ELF_INVALID_ADDRESS;
        if(is_section) {
            section_value = *((uint32_t*)start);
            start += 4;
        }

        const uint32_t offsets_count = *((uint32_t*)start);
        start += 4;

        FURI_LOG_D(
            TAG,
            "Fast relocation record %ld: is_section=%d, type=%d, hash_or_section_index=%lX, offsets_count=%ld",
            i,
            is_section,
            type,
            hash_or_section_index,
            offsets_count);

        Elf32_Addr address = 0;
        if(is_section) {
            ELFSection* symSec = elf_section_of(elf, hash_or_section_index);
            if(symSec) {
                address = (symSec->exec_addr) + section_value;
            }
        } else {
            address = elf_address_of_by_hash(elf, hash_or_section_index);
        }

        if(address == ELF_INVALID_ADDRESS) {
            FuriString* symbol_name = furi_string_alloc();
            if(elf_file_find_string_by_hash(elf, hash_or_section_index, symbol_name)) {
                FURI_LOG_E(
                    TAG,
                    "Failed to resolve address for symbol %s (hash %lX)",
                    furi_string_get_cstr(symbol_name),
                    hash_or_section_index);
            } else {
                FURI_LOG_E(
                    TAG,
                    "Failed to resolve address for hash %lX (string not found)",
                    hash_or_section_index);
            }
            furi_string_free(symbol_name);

            no_errors = false;
            start += 3 * offsets_count;
        } else {
            for(uint32_t j = 0; j < offsets_count; j++) {
                uint32_t offset = *((uint32_t*)start) & 0x00FFFFFF;
                start += 3;
                Elf32_Addr patchAddr = ((Elf32_Addr)s->data) + offset;
                Elf32_Addr relAddr = (s->exec_addr) + offset;
                elf_relocate_symbol(elf, patchAddr, relAddr, type, address);
            }
        }
    }

    aligned_free(s->fast_rel->data);
    free(s->fast_rel);
    s->fast_rel = NULL;

    return no_errors;
}

static bool elf_relocate_section(ELFFile* elf, ELFSection* section) {
    if(section->fast_rel) {
        FURI_LOG_D(TAG, "Fast relocating section");
        return elf_relocate_fast(elf, section);
    } else if(section->rel_count) {
        FURI_LOG_D(TAG, "Relocating section");
        return elf_relocate(elf, section);
    } else {
        FURI_LOG_D(TAG, "No relocation index"); /* Not an error */
    }
    return true;
}

static void elf_file_call_section_list(ELFSection* section, bool reverse_order) {
    if(section && section->size) {
        const uint32_t* start = section->data;
        const uint32_t* end = section->data + section->size;

        if(reverse_order) {
            while(end > start) {
                end--;
                ((void (*)(void))(*end))();
            }
        } else {
            while(start < end) {
                ((void (*)(void))(*start))();
                start++;
            }
        }
    }
}

/**************************************************************************************************/
/*********************************** Execute-in-Place (XIP) **************************************/
/**************************************************************************************************/

/** A section is XIP-eligible if it is allocated, read-only, has file data
 *  (not BSS) and is non-empty. Writable sections (.data/.bss) always stay
 *  in RAM. */
static bool elf_section_is_xip_eligible(const ELFSection* section) {
    return (section->sh_flags & SHF_ALLOC) && !(section->sh_flags & SHF_WRITE) &&
           section->size > 0 && section->file_offset != 0;
}

/** Compute a hash of all RAM section exec_addrs. XIP code in flash contains
 *  relocated pointers to RAM sections; if those move between launches the
 *  cache must be re-relocated. */
static uint32_t elf_compute_ram_addr_hash(ELFFile* elf) {
    uint32_t hash = 2166136261u; /* FNV-1a offset basis */
    ELFSectionDict_it_t it;
    for(ELFSectionDict_it(it, elf->sections); !ELFSectionDict_end_p(it); ELFSectionDict_next(it)) {
        ELFSectionDict_itref_t* itref = ELFSectionDict_ref(it);
        ELFSection* sec = &itref->value;
        if(!sec->xip && sec->data != NULL) {
            uint32_t addr = (uint32_t)sec->data;
            for(int b = 0; b < 4; b++) {
                hash ^= (addr >> (b * 8)) & 0xFF;
                hash *= 16777619u;
            }
        }
    }
    return hash;
}

/** Reset the bump allocator and assign flash addresses to XIP-eligible
 *  sections. Called on a fresh (cache-miss) load. */
static void elf_xip_assign_addresses(ELFFile* elf) {
    elf->xip_region.next_free = elf->xip_region.data_start;
    elf->xip_region.cache_valid = false;
    elf->xip_region.needs_rerelocation = false;

    ELFSectionDict_it_t it;

    /* Clear any stale XIP state. */
    for(ELFSectionDict_it(it, elf->sections); !ELFSectionDict_end_p(it); ELFSectionDict_next(it)) {
        ELFSectionDict_itref_t* itref = ELFSectionDict_ref(it);
        itref->value.xip = false;
    }

    /* Calculate total XIP size needed (page-aligned per section). */
    size_t xip_total = 0;
    for(ELFSectionDict_it(it, elf->sections); !ELFSectionDict_end_p(it); ELFSectionDict_next(it)) {
        ELFSectionDict_itref_t* itref = ELFSectionDict_ref(it);
        if(elf_section_is_xip_eligible(&itref->value)) {
            size_t aligned = (itref->value.size + XIP_FLASH_PAGE_SIZE - 1) &
                             ~((size_t)XIP_FLASH_PAGE_SIZE - 1);
            xip_total += aligned;
        }
    }

    if(xip_total == 0) {
        FURI_LOG_D(TAG, "No XIP-eligible sections found");
        xip_region_release(&elf->xip_region);
        return;
    }

    if(xip_total > (XIP_REGION_MAX_SIZE - XIP_CACHE_HEADER_SIZE)) {
        FURI_LOG_E(TAG, "XIP sections too large (%zu bytes), falling back to RAM", xip_total);
        xip_region_release(&elf->xip_region);
        return;
    }

    /* Allocate flash addresses for each XIP-eligible section. */
    for(ELFSectionDict_it(it, elf->sections); !ELFSectionDict_end_p(it); ELFSectionDict_next(it)) {
        ELFSectionDict_itref_t* itref = ELFSectionDict_ref(it);
        ELFSection* sec = &itref->value;
        if(elf_section_is_xip_eligible(sec)) {
            uint32_t flash_addr = xip_region_alloc(&elf->xip_region, sec->size, XIP_FLASH_PAGE_SIZE);
            if(flash_addr == 0) {
                FURI_LOG_E(TAG, "XIP alloc failed for '%s', falling back to RAM", itref->key);
                xip_region_release(&elf->xip_region);
                /* Clear any XIP flags we already set. */
                ELFSectionDict_it_t it2;
                for(ELFSectionDict_it(it2, elf->sections); !ELFSectionDict_end_p(it2);
                    ELFSectionDict_next(it2)) {
                    ELFSectionDict_ref(it2)->value.xip = false;
                }
                return;
            }
            sec->exec_addr = flash_addr;
            sec->xip = true;
            FURI_LOG_I(TAG, "Section '%s' (%lu bytes) -> XIP 0x%08lX", itref->key, sec->size, flash_addr);
        }
    }

    FURI_LOG_I(TAG, "XIP setup: %zu bytes allocated in flash", xip_region_used(&elf->xip_region));
}

/** Decide whether to use XIP for this ELF, and if so set up section flash
 *  addresses (either from cache or freshly allocated). */
static void elf_setup_xip(ELFFile* elf) {
    /* Plugins must not use XIP — they share the flash region with the main app. */
    if(elf->xip_disabled) {
        memset(&elf->xip_region, 0, sizeof(XipRegion));
        FURI_LOG_D(TAG, "XIP disabled for this ELF (plugin mode)");
        return;
    }

    /* If everything fits in RAM, skip XIP (faster, no flash wear). Only count
     * sections still needing allocation (data == NULL); BSS is pre-allocated. */
    size_t remaining_alloc_size = 0;
    ELFSectionDict_it_t ram_it;
    for(ELFSectionDict_it(ram_it, elf->sections); !ELFSectionDict_end_p(ram_it);
        ELFSectionDict_next(ram_it)) {
        ELFSectionDict_itref_t* itref = ELFSectionDict_ref(ram_it);
        if((itref->value.sh_flags & SHF_ALLOC) && itref->value.data == NULL) {
            remaining_alloc_size += itref->value.size;
        }
    }

    furi_kernel_lock();
    size_t max_block = memmgr_heap_get_max_free_block();
    furi_kernel_unlock();

    size_t ram_needed = remaining_alloc_size + 32768; /* 32KB fragmentation margin */

    if(ram_needed <= max_block && !elf->xip_forced) {
        FURI_LOG_I(
            TAG,
            "App fits in RAM (%zu bytes, %zu available) — skipping XIP",
            remaining_alloc_size,
            max_block);
        memset(&elf->xip_region, 0, sizeof(XipRegion));
        return;
    }

    if(elf->xip_forced) {
        FURI_LOG_I(TAG, "XIP forced by app (%zu bytes code)", remaining_alloc_size);
    }

    xip_region_init(&elf->xip_region);
    if(!elf->xip_region.active) return;

    /* Check whether cached XIP data is still valid for this app. */
    if(xip_cache_validate(
           &elf->xip_region,
           elf->fd,
           elf->api_interface->api_version_major,
           elf->api_interface->api_version_minor)) {
        const XipCacheHeader* header = xip_cache_get_header(&elf->xip_region);
        ELFSectionDict_it_t it;

        for(ELFSectionDict_it(it, elf->sections); !ELFSectionDict_end_p(it);
            ELFSectionDict_next(it)) {
            ELFSectionDict_itref_t* itref = ELFSectionDict_ref(it);
            ELFSection* sec = &itref->value;

            if(!elf_section_is_xip_eligible(sec)) {
                sec->xip = false;
                continue;
            }

            bool found = false;
            for(uint32_t i = 0; i < header->section_count; i++) {
                if(strncmp(itref->key, header->sections[i].name, 15) == 0 &&
                   sec->size == header->sections[i].size) {
                    sec->exec_addr = elf->xip_region.base_addr + header->sections[i].flash_offset;
                    sec->data = (void*)sec->exec_addr;
                    sec->xip = true;
                    found = true;
                    FURI_LOG_I(
                        TAG,
                        "Cache restore: '%s' (%lu bytes) at 0x%08lX",
                        itref->key,
                        sec->size,
                        sec->exec_addr);
                    break;
                }
            }

            if(!found) {
                FURI_LOG_W(TAG, "Cache miss: section '%s' not found in cache", itref->key);
                elf->xip_region.cache_valid = false;
                break;
            }
        }

        if(elf->xip_region.cache_valid) {
            FURI_LOG_I(TAG, "XIP cache hit — reusing flash contents");
            return;
        }
        FURI_LOG_I(TAG, "XIP cache invalidated during restore, doing fresh load");
    }

    elf_xip_assign_addresses(elf);
}

/** Stream one XIP section from the ELF file into flash, page by page,
 *  applying relocations per page. Only needs ~1 page + the relocation
 *  metadata in RAM at once, not the whole section.
 *
 *  @param patch_mode  When true: compare each freshly-relocated page against
 *                     what is already in flash and only reprogram pages that
 *                     differ (used for cache-hit in-place re-relocation).
 *                     When false: program every page.
 */
static bool elf_xip_stream_section(ELFFile* elf, ELFSection* sec, bool patch_mode) {
    const size_t PAGE = XIP_FLASH_PAGE_SIZE;

    /* --- Pre-resolve fast relocation records, if present --- */
    typedef struct {
        uint8_t type;
        Elf32_Addr address;
        uint32_t offsets_count;
        const uint8_t* offsets_data; /* 3 bytes per offset */
    } FastRelRecord;

    FastRelRecord* fast_records = NULL;
    uint32_t fast_records_count = 0;
    bool has_fast_rel = false;

    if(sec->fast_rel && sec->fast_rel->data && sec->fast_rel->size > 0) {
        const uint8_t* fr = sec->fast_rel->data;
        uint8_t ver = *fr;
        fr += 1;
        if(ver == FAST_RELOCATION_VERSION) {
            fast_records_count = *((uint32_t*)fr);
            fr += 4;
            fast_records = malloc(fast_records_count * sizeof(FastRelRecord));
            if(fast_records) {
                bool ok = true;
                for(uint32_t i = 0; i < fast_records_count; i++) {
                    bool is_section = (*fr & (0x1 << 7)) ? true : false;
                    fast_records[i].type = *fr & 0x7F;
                    fr += 1;
                    uint32_t hash_or_idx = *((uint32_t*)fr);
                    fr += 4;
                    uint32_t section_value = ELF_INVALID_ADDRESS;
                    if(is_section) {
                        section_value = *((uint32_t*)fr);
                        fr += 4;
                    }
                    fast_records[i].offsets_count = *((uint32_t*)fr);
                    fr += 4;
                    fast_records[i].offsets_data = fr;
                    if(is_section) {
                        ELFSection* symSec = elf_section_of(elf, hash_or_idx);
                        fast_records[i].address =
                            symSec ? (symSec->exec_addr) + section_value : ELF_INVALID_ADDRESS;
                    } else {
                        fast_records[i].address = elf_address_of_by_hash(elf, hash_or_idx);
                    }
                    if(fast_records[i].address == ELF_INVALID_ADDRESS) {
                        /* Diagnostic: report which symbol/section failed to
                         * resolve. A common culprit is a libgcc/compiler-rt
                         * helper (e.g. __paritysi2) not exported in the SDK
                         * table (api_symbols.csv) — see EXTERNAL_LIBS.md. */
                        if(is_section) {
                            FURI_LOG_E(
                                TAG,
                                "XIP: unresolved fast rel record %lu SECTION idx=%lu val=%lX",
                                i,
                                (unsigned long)hash_or_idx,
                                (unsigned long)section_value);
                        } else {
                            FuriString* sym_name = furi_string_alloc();
                            bool found =
                                elf_file_find_string_by_hash(elf, hash_or_idx, sym_name);
                            FURI_LOG_E(
                                TAG,
                                "XIP: unresolved fast rel record %lu HASH=%lX name=%s",
                                i,
                                (unsigned long)hash_or_idx,
                                found ? furi_string_get_cstr(sym_name) : "<unknown>");
                            furi_string_free(sym_name);
                        }
                        ok = false;
                    }
                    fr += 3 * fast_records[i].offsets_count;
                }
                if(ok) {
                    has_fast_rel = true;
                    FURI_LOG_I(TAG, "XIP stream: fast rel path (%lu records)", fast_records_count);
                } else {
                    free(fast_records);
                    fast_records = NULL;
                    fast_records_count = 0;
                }
            }
        }
    }

    /* --- Load standard relocations if fast rel unavailable --- */
    Elf32_Rel* rels = NULL;
    size_t rel_count = has_fast_rel ? 0 : sec->rel_count;
    if(!has_fast_rel && rel_count > 0) {
        size_t rel_bytes = rel_count * sizeof(Elf32_Rel);
        rels = malloc(rel_bytes);
        if(!rels) {
            FURI_LOG_E(TAG, "XIP: can't alloc rel table (%zu bytes)", rel_bytes);
            return false;
        }
        if(!storage_file_seek(elf->fd, sec->rel_offset, true)) {
            free(rels);
            return false;
        }
        size_t got_total = 0;
        while(got_total < rel_bytes) {
            size_t chunk = rel_bytes - got_total;
            if(chunk > 0xF000) chunk = 0xF000;
            size_t got = storage_file_read(elf->fd, (uint8_t*)rels + got_total, chunk);
            if(got == 0) {
                free(rels);
                return false;
            }
            got_total += got;
        }

        /* Pre-resolve all standard-relocation symbol addresses into the cache. */
        FuriString* sym_name = furi_string_alloc();
        for(size_t i = 0; i < rel_count; i++) {
            int symEntry = ELF32_R_SYM(rels[i].r_info);
            Elf32_Addr tmp;
            if(address_cache_get(elf->relocation_cache, symEntry, &tmp)) continue;
            Elf32_Sym sym;
            furi_string_reset(sym_name);
            if(!elf_read_symbol(elf, symEntry, &sym, sym_name)) {
                FURI_LOG_E(TAG, "XIP: symbol read fail");
                furi_string_free(sym_name);
                free(rels);
                return false;
            }
            Elf32_Addr symAddr = elf_address_of(elf, &sym, furi_string_get_cstr(sym_name));
            address_cache_put(elf->relocation_cache, symEntry, symAddr);
        }
        furi_string_free(sym_name);
    }

    /* --- Page-by-page streaming --- */
    uint8_t* page_buf = malloc(PAGE);
    if(!page_buf) {
        FURI_LOG_E(TAG, "XIP: page buffer alloc failed");
        if(rels) free(rels);
        if(fast_records) free(fast_records);
        return false;
    }

    bool ok = true;
    for(size_t page_start = 0; page_start < sec->size && ok; page_start += PAGE) {
        size_t page_len = sec->size - page_start;
        if(page_len > PAGE) page_len = PAGE;

        /* Fill page: read raw section bytes, zero-pad tail. */
        memset(page_buf, 0, PAGE);
        if(!storage_file_seek(elf->fd, sec->file_offset + page_start, true)) {
            FURI_LOG_E(TAG, "XIP: seek fail");
            ok = false;
            break;
        }
        size_t got_total = 0;
        while(got_total < page_len) {
            size_t got = storage_file_read(elf->fd, page_buf + got_total, page_len - got_total);
            if(got == 0) {
                FURI_LOG_E(TAG, "XIP: read fail");
                ok = false;
                break;
            }
            got_total += got;
        }
        if(!ok) break;

        /* Apply relocations that land inside this page. patchAddr targets the
         * staging page buffer (page_buf), relAddr uses the final flash exec
         * address so PC-relative offsets are correct. */
        Elf32_Addr page_flash = sec->exec_addr + page_start;

        if(has_fast_rel) {
            for(uint32_t r = 0; r < fast_records_count; r++) {
                const uint8_t* off_ptr = fast_records[r].offsets_data;
                for(uint32_t j = 0; j < fast_records[r].offsets_count; j++) {
                    uint32_t off = (off_ptr[0] | (off_ptr[1] << 8) | (off_ptr[2] << 16));
                    off_ptr += 3;
                    /* Relocation writes up to 4 bytes; require it fully inside page. */
                    if(off >= page_start && (off + 4) <= (page_start + page_len)) {
                        Elf32_Addr patchAddr = (Elf32_Addr)(page_buf + (off - page_start));
                        Elf32_Addr relAddr = sec->exec_addr + off;
                        elf_relocate_symbol(
                            elf, patchAddr, relAddr, fast_records[r].type, fast_records[r].address);
                    }
                }
            }
        } else {
            for(size_t i = 0; i < rel_count; i++) {
                uint32_t off = rels[i].r_offset;
                if(off >= page_start && (off + 4) <= (page_start + page_len)) {
                    int symEntry = ELF32_R_SYM(rels[i].r_info);
                    int relType = ELF32_R_TYPE(rels[i].r_info);
                    Elf32_Addr symAddr;
                    if(!address_cache_get(elf->relocation_cache, symEntry, &symAddr)) {
                        symAddr = ELF_INVALID_ADDRESS;
                    }
                    if(symAddr == ELF_INVALID_ADDRESS) {
                        FURI_LOG_E(TAG, "XIP: unresolved sym in page reloc");
                        ok = false;
                        break;
                    }
                    Elf32_Addr patchAddr = (Elf32_Addr)(page_buf + (off - page_start));
                    Elf32_Addr relAddr = sec->exec_addr + off;
                    if(!elf_relocate_symbol(elf, patchAddr, relAddr, relType, symAddr)) {
                        ok = false;
                        break;
                    }
                }
            }
        }
        if(!ok) break;

        /* Program the page. In patch mode, skip pages already matching flash. */
        if(patch_mode) {
            const uint8_t* flash_ptr = (const uint8_t*)page_flash;
            if(memcmp(flash_ptr, page_buf, PAGE) == 0) {
                continue; /* unchanged, no erase/write */
            }
        }

        if(!xip_region_program_page(&elf->xip_region, page_flash, page_buf)) {
            FURI_LOG_E(TAG, "XIP: program page 0x%08lX failed", page_flash);
            ok = false;
            break;
        }

        /* Yield periodically so the BLE stack can service events. */
        furi_delay_tick(1);
    }

    free(page_buf);
    if(rels) free(rels);
    if(fast_records) free(fast_records);

    if(ok) {
        /* Section now executes from flash. */
        sec->data = (void*)sec->exec_addr;
        FURI_LOG_I(TAG, "XIP section committed at 0x%08lX (%lu bytes)", sec->exec_addr, sec->size);
    }
    return ok;
}

/**************************************************************************************************/
/********************************************* Public *********************************************/
/**************************************************************************************************/

void elf_file_disable_xip(ELFFile* elf) {
    furi_check(elf);
    elf->xip_disabled = true;
}

void elf_file_force_xip(ELFFile* elf) {
    furi_check(elf);
    elf->xip_forced = true;
}

ELFFile* elf_file_alloc(Storage* storage, const ElfApiInterface* api_interface) {
    ELFFile* elf = malloc(sizeof(ELFFile));
    elf->fd = storage_file_alloc(storage);
    elf->api_interface = api_interface;
    ELFSectionDict_init(elf->sections);
    AddressCache_init(elf->trampoline_cache);
    elf->init_array_called = false;
    memset(&elf->xip_region, 0, sizeof(XipRegion));
    elf->xip_disabled = false;
    elf->xip_forced = false;
    return elf;
}

void elf_file_free(ELFFile* elf) {
    // furi_check(!elf->init_array_called);
    if(elf->init_array_called) {
        FURI_LOG_W(TAG, "Init array was called, but fini array wasn't");
        elf_file_call_section_list(elf->fini_array, true);
    }

    /* Release the XIP flash region so the next app can use it. */
    xip_region_release(&elf->xip_region);

    // free sections data
    {
        ELFSectionDict_it_t it;
        for(ELFSectionDict_it(it, elf->sections); !ELFSectionDict_end_p(it);
            ELFSectionDict_next(it)) {
            const ELFSectionDict_itref_t* itref = ELFSectionDict_cref(it);
            /* XIP sections point into flash (data == exec_addr) — never free. */
            if(!itref->value.xip) {
                aligned_free(itref->value.data);
            }
            if(itref->value.fast_rel) {
                if(itref->value.fast_rel->data) {
                    aligned_free(itref->value.fast_rel->data);
                }
                free(itref->value.fast_rel);
            }
            free((void*)itref->key);
        }

        ELFSectionDict_clear(elf->sections);
    }

    // free trampoline data
    {
        AddressCache_it_t it;
        for(AddressCache_it(it, elf->trampoline_cache); !AddressCache_end_p(it);
            AddressCache_next(it)) {
            const AddressCache_itref_t* itref = AddressCache_cref(it);
            free((void*)itref->value);
        }

        AddressCache_clear(elf->trampoline_cache);
    }

    if(elf->debug_link_info.debug_link) {
        free(elf->debug_link_info.debug_link);
    }

    elf_file_maybe_release_fd(elf);
    free(elf);
}

bool elf_file_open(ELFFile* elf, const char* path) {
    Elf32_Ehdr h;
    Elf32_Shdr sH;

    if(!storage_file_open(elf->fd, path, FSAM_READ, FSOM_OPEN_EXISTING) ||
       !storage_file_seek(elf->fd, 0, true) ||
       storage_file_read(elf->fd, &h, sizeof(h)) != sizeof(h) ||
       !storage_file_seek(elf->fd, h.e_shoff + h.e_shstrndx * sizeof(sH), true) ||
       storage_file_read(elf->fd, &sH, sizeof(Elf32_Shdr)) != sizeof(Elf32_Shdr)) {
        return false;
    }

    elf->entry = h.e_entry;
    elf->sections_count = h.e_shnum;
    elf->section_table = h.e_shoff;
    elf->section_table_strings = sH.sh_offset;
    return true;
}

ElfLoadSectionTableResult elf_file_load_section_table(ELFFile* elf) {
    SectionType loaded_sections = 0;
    FuriString* name = furi_string_alloc();
    ElfLoadSectionTableResult result = ElfLoadSectionTableResultSuccess;

    FURI_LOG_D(TAG, "Scan ELF indexs...");

    for(size_t section_idx = 1; section_idx < elf->sections_count; section_idx++) {
        Elf32_Shdr section_header;

        furi_string_reset(name);
        if(!elf_read_section(elf, section_idx, &section_header, name)) {
            loaded_sections = 0;
            break;
        }

        FURI_LOG_D(
            TAG, "Preloading data for section #%d %s", section_idx, furi_string_get_cstr(name));
        SectionTypeInfo section_type_info =
            elf_preload_section(elf, section_idx, &section_header, name);
        loaded_sections |= section_type_info.type;

        if(section_type_info.result != ELFLoadSectionResultSuccess) {
            if(section_type_info.result == ELFLoadSectionResultNoMemory) {
                FURI_LOG_E(TAG, "Not enough memory");
                result = ElfLoadSectionTableResultNoMemory;
            } else if(section_type_info.result == ELFLoadSectionResultError) {
                FURI_LOG_E(TAG, "Error loading section");
                result = ElfLoadSectionTableResultError;
            }

            loaded_sections = 0;
            break;
        }
    }

    furi_string_free(name);

    if(result != ElfLoadSectionTableResultSuccess) {
        return result;
    }

    bool sections_valid = IS_FLAGS_SET(loaded_sections, SectionTypeSymTab | SectionTypeStrTab) |
                          IS_FLAGS_SET(loaded_sections, SectionTypeFastRelData);
    if(!sections_valid) {
        FURI_LOG_E(TAG, "No valid sections found");
        return ElfLoadSectionTableResultError;
    }

    /* Relocation cache is needed during XIP setup (cache-hit symbol resolve)
     * and section streaming; initialise it here. */
    AddressCache_init(elf->relocation_cache);

    /* Decide RAM vs XIP for read-only sections. Section data has NOT been
     * loaded yet (deferred) — XIP setup uses only metadata. */
    elf_setup_xip(elf);

    /* Materialize non-XIP sections into RAM now. XIP sections stay deferred;
     * they are streamed to flash one page at a time in elf_file_load_sections
     * to keep peak RAM usage low. */
    ELFSectionDict_it_t it;
    for(ELFSectionDict_it(it, elf->sections); !ELFSectionDict_end_p(it); ELFSectionDict_next(it)) {
        ELFSectionDict_itref_t* itref = ELFSectionDict_ref(it);
        ELFSection* sec = &itref->value;
        if(!sec->xip) {
            ELFLoadSectionResult res = elf_materialize_section(elf, sec);
            if(res == ELFLoadSectionResultNoMemory) {
                return ElfLoadSectionTableResultNoMemory;
            } else if(res != ELFLoadSectionResultSuccess) {
                return ElfLoadSectionTableResultError;
            }
            sec->exec_addr = (Elf32_Addr)sec->data;
        }
    }

    /* If we restored from an XIP cache, check whether RAM sections landed at
     * the same addresses. If not, the XIP flash contains stale pointers and
     * must be re-relocated in place. */
    if(elf->xip_region.active && elf->xip_region.cache_valid) {
        uint32_t current_hash = elf_compute_ram_addr_hash(elf);
        const XipCacheHeader* hdr = xip_cache_get_header(&elf->xip_region);
        if(!hdr || hdr->ram_addr_hash != current_hash) {
            FURI_LOG_I(
                TAG,
                "RAM addresses changed (cached=%08lX, current=%08lX) — will re-relocate XIP",
                hdr ? hdr->ram_addr_hash : 0,
                current_hash);
            elf->xip_region.needs_rerelocation = true;
        }
    }

    return ElfLoadSectionTableResultSuccess;
}

ElfProcessSectionResult elf_process_section(
    ELFFile* elf,
    const char* name,
    ElfProcessSection* process_section,
    void* context) {
    ElfProcessSectionResult result = ElfProcessSectionResultNotFound;
    FuriString* section_name = furi_string_alloc();
    Elf32_Shdr section_header;

    // find section
    for(size_t section_idx = 1; section_idx < elf->sections_count; section_idx++) {
        furi_string_reset(section_name);
        if(!elf_read_section(elf, section_idx, &section_header, section_name)) {
            break;
        }

        if(furi_string_cmp(section_name, name) == 0) {
            result = ElfProcessSectionResultCannotProcess;
            break;
        }
    }

    if(result != ElfProcessSectionResultNotFound) { //-V547
        if(process_section(elf->fd, section_header.sh_offset, section_header.sh_size, context)) {
            result = ElfProcessSectionResultSuccess;
        } else {
            result = ElfProcessSectionResultCannotProcess; //-V1048
        }
    }

    furi_string_free(section_name);

    return result;
}

/** Build and commit the XIP cache header describing the sections just
 *  written to flash, so a future launch of the same app can skip the work. */
static void elf_xip_commit_cache(ELFFile* elf) {
    if(!elf->xip_region.active) return;

    XipCacheHeader header;
    memset(&header, 0, sizeof(header));
    header.magic = XIP_CACHE_MAGIC;
    header.file_size = (uint32_t)storage_file_size(elf->fd);
    storage_file_seek(elf->fd, 0, true);
    header.file_crc32 = crc32_calc_file(elf->fd, NULL, NULL);
    storage_file_seek(elf->fd, 0, true);
    header.api_version = ((uint32_t)elf->api_interface->api_version_major << 16) |
                         elf->api_interface->api_version_minor;
    header.ram_addr_hash = elf_compute_ram_addr_hash(elf);

    uint32_t count = 0;
    ELFSectionDict_it_t it;
    for(ELFSectionDict_it(it, elf->sections); !ELFSectionDict_end_p(it); ELFSectionDict_next(it)) {
        ELFSectionDict_itref_t* itref = ELFSectionDict_ref(it);
        ELFSection* sec = &itref->value;
        if(sec->xip && count < XIP_CACHE_MAX_SECTIONS) {
            header.sections[count].flash_offset = sec->exec_addr - elf->xip_region.base_addr;
            header.sections[count].size = sec->size;
            strncpy(header.sections[count].name, itref->key, 15);
            header.sections[count].name[15] = '\0';
            count++;
        }
    }
    header.section_count = count;

    xip_cache_commit_header(&elf->xip_region, &header);
}

ELFFileLoadStatus elf_file_load_sections(ELFFile* elf) {
    furi_check(elf->fd != NULL);
    ELFFileLoadStatus status = ELFFileLoadStatusSuccess;
    ELFSectionDict_it_t it;

    /* relocation_cache was initialised in elf_file_load_section_table. */

    bool xip_active = elf->xip_region.active;
    bool cache_hit = xip_active && elf->xip_region.cache_valid;
    bool needs_rereloc = xip_active && elf->xip_region.needs_rerelocation;

    if(cache_hit && !needs_rereloc) {
        /* Fast path: flash already holds fully-relocated code from a previous
         * launch and RAM sections landed at the same addresses. Only RAM
         * sections need (re)relocation this launch. */
        FURI_LOG_I(TAG, "XIP cache hit — relocating RAM sections only");
        for(ELFSectionDict_it(it, elf->sections); !ELFSectionDict_end_p(it);
            ELFSectionDict_next(it)) {
            ELFSectionDict_itref_t* itref = ELFSectionDict_ref(it);
            ELFSection* sec = &itref->value;
            if(!sec->xip) {
                if(!elf_relocate_section(elf, sec)) {
                    FURI_LOG_E(TAG, "Error relocating RAM section '%s'", itref->key);
                    status = ELFFileLoadStatusMissingImports;
                }
            } else if(sec->fast_rel) {
                /* XIP section reused from cache — drop unused fast_rel blob. */
                if(sec->fast_rel->data) aligned_free(sec->fast_rel->data);
                free(sec->fast_rel);
                sec->fast_rel = NULL;
            }
        }
    } else {
        /* Fresh load, or cache hit that needs in-place re-relocation. */
        for(ELFSectionDict_it(it, elf->sections); !ELFSectionDict_end_p(it);
            ELFSectionDict_next(it)) {
            ELFSectionDict_itref_t* itref = ELFSectionDict_ref(it);
            ELFSection* sec = &itref->value;

            if(sec->xip) {
                FURI_LOG_D(TAG, "Streaming XIP section '%s' (patch=%d)", itref->key, needs_rereloc);
                if(!elf_xip_stream_section(elf, sec, needs_rereloc)) {
                    FURI_LOG_E(TAG, "Error streaming XIP section '%s'", itref->key);
                    status = ELFFileLoadStatusMissingImports;
                }
                /* Free fast_rel blob now that streaming consumed it. */
                if(sec->fast_rel) {
                    if(sec->fast_rel->data) aligned_free(sec->fast_rel->data);
                    free(sec->fast_rel);
                    sec->fast_rel = NULL;
                }
            } else {
                FURI_LOG_D(TAG, "Relocating section '%s'", itref->key);
                if(!elf_relocate_section(elf, sec)) {
                    FURI_LOG_E(TAG, "Error relocating section '%s'", itref->key);
                    status = ELFFileLoadStatusMissingImports;
                }
            }
        }

        /* Persist the cache header so a future launch can skip this. */
        if(status == ELFFileLoadStatusSuccess && xip_active) {
            elf_xip_commit_cache(elf);
        }
    }

    /* Fixing up entry point — use exec_addr (flash for XIP .text). */
    if(status == ELFFileLoadStatusSuccess) {
        ELFSection* text_section = elf_file_get_section(elf, ".text");

        if(text_section == NULL) {
            FURI_LOG_E(TAG, "No .text section found");
            status = ELFFileLoadStatusUnspecifiedError;
        } else {
            elf->entry += (uint32_t)text_section->exec_addr;
        }
    }

    FURI_LOG_D(TAG, "Relocation cache size: %u", AddressCache_size(elf->relocation_cache));
    FURI_LOG_D(TAG, "Trampoline cache size: %u", AddressCache_size(elf->trampoline_cache));
    AddressCache_clear(elf->relocation_cache);

    {
        size_t total_size = 0;
        for(ELFSectionDict_it(it, elf->sections); !ELFSectionDict_end_p(it);
            ELFSectionDict_next(it)) {
            ELFSectionDict_itref_t* itref = ELFSectionDict_ref(it);
            total_size += itref->value.size;
        }
        FURI_LOG_I(TAG, "Total size of loaded sections: %zu", total_size);
    }

    elf_file_maybe_release_fd(elf);
    return status;
}

void elf_file_call_init(ELFFile* elf) {
    furi_check(!elf->init_array_called);
    elf_file_call_section_list(elf->preinit_array, false);
    elf_file_call_section_list(elf->init_array, false);
    elf->init_array_called = true;
}

bool elf_file_is_init_complete(ELFFile* elf) {
    return elf->init_array_called;
}

void* elf_file_get_entry_point(ELFFile* elf) {
    furi_check(elf->init_array_called);
    return (void*)elf->entry;
}

void elf_file_call_fini(ELFFile* elf) {
    furi_check(elf->init_array_called);
    elf_file_call_section_list(elf->fini_array, true);
    elf->init_array_called = false;
}

const ElfApiInterface* elf_file_get_api_interface(ELFFile* elf_file) {
    return elf_file->api_interface;
}

void elf_file_init_debug_info(ELFFile* elf, ELFDebugInfo* debug_info) {
    // set entry
    debug_info->entry = elf->entry;

    // copy debug info
    memcpy(&debug_info->debug_link_info, &elf->debug_link_info, sizeof(ELFDebugLinkInfo));

    // init mmap
    debug_info->mmap_entry_count = ELFSectionDict_size(elf->sections);
    debug_info->mmap_entries = malloc(sizeof(ELFMemoryMapEntry) * debug_info->mmap_entry_count);
    uint32_t mmap_entry_idx = 0;

    ELFSectionDict_it_t it;
    for(ELFSectionDict_it(it, elf->sections); !ELFSectionDict_end_p(it); ELFSectionDict_next(it)) {
        const ELFSectionDict_itref_t* itref = ELFSectionDict_cref(it);

        const void* data_ptr = itref->value.data;
        if(data_ptr) {
            ELFMemoryMapEntry* entry = &debug_info->mmap_entries[mmap_entry_idx];
            entry->address = (uint32_t)data_ptr;
            entry->name = itref->key;
            mmap_entry_idx++;
        }
    }
}

void elf_file_clear_debug_info(ELFDebugInfo* debug_info) {
    // clear debug info
    memset(&debug_info->debug_link_info, 0, sizeof(ELFDebugLinkInfo));

    // clear mmap
    if(debug_info->mmap_entries) {
        free(debug_info->mmap_entries);
        debug_info->mmap_entries = NULL;
    }

    debug_info->mmap_entry_count = 0;
}
