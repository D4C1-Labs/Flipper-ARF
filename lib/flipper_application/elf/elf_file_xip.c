#include "elf_file_xip.h"
#include <furi_hal_flash.h>
#include <furi.h>
#include <toolbox/crc32_calc.h>

#define TAG "XIP"

static bool xip_region_in_use = false;

void xip_region_init(XipRegion* region) {
    furi_check(region);
    memset(region, 0, sizeof(XipRegion));

    if(xip_region_in_use) {
        FURI_LOG_W(TAG, "XIP region already in use by another app, skipping");
        return;
    }

    size_t free_start = furi_hal_flash_get_free_page_start_address();
    size_t free_end = (size_t)furi_hal_flash_get_free_end_address();

    if(free_end <= free_start) {
        FURI_LOG_W(TAG, "No free flash available");
        return;
    }

    size_t free_size = free_end - free_start;
    size_t region_size = MIN(free_size, (size_t)XIP_REGION_MAX_SIZE);

    /* Round region size down to a whole page. */
    region_size -= region_size % XIP_FLASH_PAGE_SIZE;

    if(region_size < XIP_REGION_MIN_SIZE) {
        FURI_LOG_W(
            TAG,
            "Flash too fragmented for XIP: %zu available, %u minimum",
            region_size,
            XIP_REGION_MIN_SIZE);
        return;
    }

    region->base_addr = (uint32_t)free_start;
    region->end_addr = region->base_addr + region_size;
    /* Reserve one page for the cache header at the start of the region. */
    region->data_start = region->base_addr + XIP_CACHE_HEADER_SIZE;
    region->next_free = region->data_start;
    region->active = true;
    region->cache_valid = false;
    xip_region_in_use = true;

    FURI_LOG_I(
        TAG,
        "Region initialized: 0x%08lX - 0x%08lX (%zu KB, header at base)",
        region->base_addr,
        region->end_addr,
        region_size / 1024);
}

void xip_region_release(XipRegion* region) {
    furi_check(region);
    if(region->active) {
        xip_region_in_use = false;
        region->active = false;
        FURI_LOG_D(TAG, "Region released");
    }
}

bool xip_cache_validate(
    XipRegion* region,
    File* fd,
    uint16_t api_version_major,
    uint16_t api_version_minor) {
    furi_check(region);
    furi_check(fd);

    if(!region->active) return false;

    /* Read cache header directly from flash (memory-mapped on STM32). */
    const XipCacheHeader* header = (const XipCacheHeader*)(region->base_addr);

    /* Tier 1: Quick checks (instant, no I/O). */
    if(header->magic != XIP_CACHE_MAGIC) {
        FURI_LOG_I(TAG, "Cache miss: no valid header (magic=0x%08lX)", header->magic);
        return false;
    }

    uint32_t current_api = ((uint32_t)api_version_major << 16) | api_version_minor;
    if(header->api_version != current_api) {
        FURI_LOG_I(
            TAG,
            "Cache miss: API version mismatch (cached=%08lX, current=%08lX)",
            header->api_version,
            current_api);
        return false;
    }

    uint64_t file_size = storage_file_size(fd);
    if(header->file_size != (uint32_t)file_size) {
        FURI_LOG_I(
            TAG,
            "Cache miss: file size mismatch (cached=%lu, current=%llu)",
            header->file_size,
            file_size);
        return false;
    }

    if(header->section_count == 0 || header->section_count > XIP_CACHE_MAX_SECTIONS) {
        FURI_LOG_I(TAG, "Cache miss: invalid section count %lu", header->section_count);
        return false;
    }

    /* Tier 2: CRC check (requires reading the entire FAP file). */
    uint32_t tick_start = furi_get_tick();
    uint32_t file_crc = crc32_calc_file(fd, NULL, NULL);
    storage_file_seek(fd, 0, true); /* Reset position after full-file read. */
    uint32_t crc_ms = furi_get_tick() - tick_start;

    if(header->file_crc32 != file_crc) {
        FURI_LOG_I(
            TAG,
            "Cache miss: CRC mismatch (cached=%08lX, computed=%08lX, %lums)",
            header->file_crc32,
            file_crc,
            crc_ms);
        return false;
    }

    FURI_LOG_I(
        TAG,
        "Cache HIT: %lu sections, CRC %08lX verified in %lums",
        header->section_count,
        file_crc,
        crc_ms);

    region->cache_valid = true;
    return true;
}

bool xip_cache_commit_header(XipRegion* region, const XipCacheHeader* header) {
    furi_check(region);
    furi_check(header);

    if(!region->active) return false;

    /* The header lives on its own page at the base of the region. Build a
     * full-page buffer (zero-padded) and program the whole page atomically. */
    uint8_t* page = malloc(XIP_FLASH_PAGE_SIZE);
    if(!page) {
        FURI_LOG_E(TAG, "Cache header: page buffer alloc failed");
        return false;
    }
    memset(page, 0, XIP_FLASH_PAGE_SIZE);
    memcpy(page, header, sizeof(XipCacheHeader));

    int16_t page_num = furi_hal_flash_get_page_number(region->base_addr);
    furi_check(page_num >= 0);
    furi_hal_flash_program_page((uint8_t)page_num, page, XIP_FLASH_PAGE_SIZE);

    free(page);

    FURI_LOG_I(
        TAG,
        "Cache header written: %lu sections, CRC %08lX",
        header->section_count,
        header->file_crc32);

    return true;
}

const XipCacheHeader* xip_cache_get_header(const XipRegion* region) {
    if(!region->active) return NULL;
    return (const XipCacheHeader*)(region->base_addr);
}

uint32_t xip_region_alloc(XipRegion* region, size_t size, size_t alignment) {
    furi_check(region);
    if(!region->active || size == 0) return 0;

    /* Force each section onto its own page so we can program/erase page by
     * page without disturbing neighbouring sections. */
    if(alignment < XIP_FLASH_PAGE_SIZE) alignment = XIP_FLASH_PAGE_SIZE;

    uint32_t aligned = (region->next_free + alignment - 1) & ~(alignment - 1);

    if(aligned + size > region->end_addr) {
        FURI_LOG_E(
            TAG,
            "Alloc failed: need %zu at 0x%08lX, end 0x%08lX",
            size,
            aligned,
            region->end_addr);
        return 0;
    }

    region->next_free = aligned + size;

    FURI_LOG_D(TAG, "Alloc %zu bytes at 0x%08lX", size, aligned);
    return aligned;
}

bool xip_region_program_page(XipRegion* region, uint32_t flash_addr, const void* page_data) {
    furi_check(region);
    furi_check(page_data);

    if(!region->active) return false;

    /* Bounds + alignment check. */
    if(flash_addr < region->base_addr ||
       (flash_addr + XIP_FLASH_PAGE_SIZE) > region->end_addr) {
        FURI_LOG_E(TAG, "Program page out of bounds: 0x%08lX", flash_addr);
        return false;
    }
    if(flash_addr % XIP_FLASH_PAGE_SIZE) {
        FURI_LOG_E(TAG, "Program page not page-aligned: 0x%08lX", flash_addr);
        return false;
    }

    int16_t page_num = furi_hal_flash_get_page_number(flash_addr);
    if(page_num < 0) {
        FURI_LOG_E(TAG, "Program page: invalid page for 0x%08lX", flash_addr);
        return false;
    }

    /* furi_hal_flash_program_page erases the page then writes it. */
    furi_hal_flash_program_page((uint8_t)page_num, (const uint8_t*)page_data, XIP_FLASH_PAGE_SIZE);

    return true;
}

size_t xip_region_used(const XipRegion* region) {
    furi_check(region);
    if(!region->active) return 0;
    return region->next_free - region->data_start;
}
