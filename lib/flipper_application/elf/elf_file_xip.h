#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <storage/storage.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum (ceiling) size of the XIP flash region. The region is sized
 *  dynamically from whatever free flash remains after the firmware image
 *  and the BLE stack security boundary (SFSA); this is the upper bound
 *  above which we don't grow. 384KB = 96 pages of 4KB.
 *
 *  The SubGHz FAP with the full protocol catalog has ~352KB of read-only
 *  (.text + .rodata) sections, so the ceiling must exceed that. Flipper-ARF
 *  ships the BLE *Light* radio stack, which reserves far less flash than BLE
 *  Full, so on most units the free region is large enough to hold it. */
#define XIP_REGION_MAX_SIZE (384 * 1024)

/** Minimum viable XIP region. Below this, XIP stays inactive and all
 *  apps fall back to RAM-only loading. */
#define XIP_REGION_MIN_SIZE (64 * 1024)

/** STM32WB55 flash page size (bytes) — must match furi_hal_flash. */
#define XIP_FLASH_PAGE_SIZE (4096U)

/** Cache header magic value ("XIPC") */
#define XIP_CACHE_MAGIC 0x58495043

/** Maximum cached sections */
#define XIP_CACHE_MAX_SECTIONS 8

/** Size reserved at start of XIP region for cache header. Must be a whole
 *  flash page so the header lives on its own erasable page and never mixes
 *  with section data. */
#define XIP_CACHE_HEADER_SIZE XIP_FLASH_PAGE_SIZE

/** Per-section cache entry */
typedef struct {
    uint32_t flash_offset; /**< Offset from XIP base where section data starts */
    uint32_t size; /**< Section size in bytes */
    char name[16]; /**< Section name (".text", ".rodata", etc.) */
} XipCacheSectionEntry;

/** Cache header stored at the start of the XIP flash region.
 *  Allows skipping erase/write when re-launching the same app.
 */
typedef struct {
    uint32_t magic; /**< XIP_CACHE_MAGIC */
    uint32_t file_size; /**< FAP file size (quick validation) */
    uint32_t file_crc32; /**< CRC32 of FAP file (definitive validation) */
    uint32_t api_version; /**< Firmware API version (major << 16 | minor) */
    uint32_t section_count; /**< Number of cached sections */
    uint32_t ram_addr_hash; /**< Hash of RAM section exec_addrs at cache time;
                                 if RAM sections land at different addresses on
                                 next launch the cache must be re-relocated
                                 because XIP code contains relocated pointers
                                 to those addrs */
    XipCacheSectionEntry sections[XIP_CACHE_MAX_SECTIONS];
} XipCacheHeader;

/** XIP flash region bump allocator.
 *  Manages a region of internal flash used for execute-in-place loading
 *  of FAP .text and .rodata sections.
 */
typedef struct {
    uint32_t base_addr; /**< Flash start address (page-aligned) */
    uint32_t end_addr; /**< Flash end address */
    uint32_t data_start; /**< Where section data begins (after cache header) */
    uint32_t next_free; /**< Next available address (bump pointer) */
    bool active; /**< Whether XIP is available */
    bool cache_valid; /**< True if cached XIP data matches current app */
    bool needs_rerelocation; /**< Cache hit but RAM addrs changed — patch in place */
} XipRegion;

/** Initialize XIP region from free flash.
 *  Reserves space for the cache header at the start.
 *  Sets region->active = true on success.
 */
void xip_region_init(XipRegion* region);

/** Validate the XIP cache against the current FAP file.
 *  Reads the cache header from flash and compares file size, CRC, and API version.
 *  @return true if cache is valid (can skip erase/write)
 */
bool xip_cache_validate(
    XipRegion* region,
    File* fd,
    uint16_t api_version_major,
    uint16_t api_version_minor);

/** Write a pre-built cache header to the XIP flash region.
 *  The header page is erased+written as part of this call.
 */
bool xip_cache_commit_header(XipRegion* region, const XipCacheHeader* header);

/** Get the cached header from flash (read-only, memory-mapped). */
const XipCacheHeader* xip_cache_get_header(const XipRegion* region);

/** Allocate address space from the XIP region (bump allocator).
 *  Does NOT erase or write flash — just advances the pointer.
 *  Alignment is rounded up to a whole flash page so every section starts on
 *  its own page (simplifies page-granular program/erase on ARF).
 */
uint32_t xip_region_alloc(XipRegion* region, size_t size, size_t alignment);

/** Program one full flash page of the XIP region.
 *  On Flipper-ARF this maps to furi_hal_flash_program_page(), which performs
 *  an erase+write of the whole page atomically. @p data must be
 *  XIP_FLASH_PAGE_SIZE bytes (tail padded by the caller).
 *
 *  @param region     XIP region (for bounds checking)
 *  @param flash_addr destination address (must be page-aligned, in region)
 *  @param page_data  full-page (XIP_FLASH_PAGE_SIZE) source buffer in RAM
 *  @return           true on success
 */
bool xip_region_program_page(XipRegion* region, uint32_t flash_addr, const void* page_data);

/** Release the XIP region so another app can use it. */
void xip_region_release(XipRegion* region);

/** Get total bytes allocated so far. */
size_t xip_region_used(const XipRegion* region);

#ifdef __cplusplus
}
#endif
