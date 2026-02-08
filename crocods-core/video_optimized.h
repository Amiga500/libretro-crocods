/**
 * @file video_optimized.h
 * @brief Video rendering optimizations for ARM Cortex-A7 (Miyoo Mini)
 * 
 * This file provides optimizations for CRTC/Gate Array rendering:
 * - Cache-aligned data structures
 * - Optimized palette conversion
 * - Dirty rectangle tracking
 * - Fast blitting functions
 */

#ifndef VIDEO_OPTIMIZED_H
#define VIDEO_OPTIMIZED_H

#include "z80_optimized.h"  /* For LIKELY, UNLIKELY, FORCE_INLINE, etc. */

/**
 * Fast RGB565 color conversion with compile-time constants
 * Using bitwise operations optimized for ARM
 */
static FORCE_INLINE u16 RGB565_FAST(u8 r, u8 g, u8 b)
{
    /* ARM can do these shifts efficiently in a single instruction */
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3);
}

/**
 * Optimized palette lookup with prefetch
 * This reduces cache misses when accessing palette data
 * 
 * Note: pen_index is guaranteed to be in range [0-31] by the CPC hardware
 * Gate Array only has 17 pens (0-15 + border), and TabCoul array is [32]
 */
static FORCE_INLINE u16 PALETTE_LOOKUP_OPT(core_crocods_t *core, u8 pen_index)
{
    /* Bounds check in all builds for safety */
    if (UNLIKELY(pen_index > 31)) {
        pen_index = 0;  /* Fallback to pen 0 */
    }
    
    /* Direct lookup with bounds check above */
    const u8 color_index = core->TabCoul[pen_index];
    return core->BG_PALETTE[color_index];
}

/**
 * Bulk palette conversion - convert 4 palette indices to RGB565 in one go
 * Uses word-sized writes for better memory bandwidth
 */
static FORCE_INLINE void CONVERT_4_COLORS(core_crocods_t *core, u16 *dest, 
                                          const u8 *pal_indices)
{
    /* Unrolled loop for better performance */
    dest[0] = core->BG_PALETTE[core->TabCoul[pal_indices[0]]];
    dest[1] = core->BG_PALETTE[core->TabCoul[pal_indices[1]]];
    dest[2] = core->BG_PALETTE[core->TabCoul[pal_indices[2]]];
    dest[3] = core->BG_PALETTE[core->TabCoul[pal_indices[3]]];
}

/**
 * Fast 32-bit aligned memory copy
 * Copies pixels in 32-bit chunks (2 pixels at a time for RGB565)
 * 
 * Note: This function checks 4-byte alignment before using 32-bit operations
 * to avoid unaligned access faults on ARM.
 */
static FORCE_INLINE void MEMCPY_32BIT_ALIGNED(u16 *dest, const u16 *src, size_t num_pixels)
{
    /* Check if both pointers are 4-byte (32-bit) aligned */
    const int is_4byte_aligned = (((uintptr_t)dest & 3) == 0) && (((uintptr_t)src & 3) == 0);
    
    if (LIKELY(is_4byte_aligned && num_pixels >= 2)) {
        /* Process 2 pixels (4 bytes) at a time */
        const size_t num_words = num_pixels >> 1;
        u32 *dest32 = (u32 *)dest;
        const u32 *src32 = (const u32 *)src;
        
        for (size_t i = 0; i < num_words; i++) {
            dest32[i] = src32[i];
        }
        
        /* Handle odd pixel if any */
        if (num_pixels & 1) {
            dest[num_pixels - 1] = src[num_pixels - 1];
        }
    } else {
        /* Fallback to 16-bit copy if not 4-byte aligned */
        for (size_t i = 0; i < num_pixels; i++) {
            dest[i] = src[i];
        }
    }
}

/**
 * Fast screen line blit with prefetch
 * Copies a scanline with cache prefetch hints
 */
static FORCE_INLINE void BLIT_LINE_FAST(u16 *dest, const u16 *src, int width)
{
    /* Prefetch next cache line */
    PREFETCH(src + 32);
    
    /* Use word-aligned copy for better performance */
    if (LIKELY((width & 1) == 0)) {
        /* Even number of pixels - use 32-bit copy */
        MEMCPY_32BIT_ALIGNED(dest, src, width);
    } else {
        /* Fallback to regular copy for odd widths */
        for (int i = 0; i < width; i++) {
            dest[i] = src[i];
        }
    }
}

/**
 * Dirty rectangle structure for tracking screen updates
 * Aligned to cache line for better performance
 */
typedef struct {
    u8 dirty_lines[288];  /* One byte per scanline (0=clean, 1=dirty) */
    int min_dirty_line;   /* First dirty line */
    int max_dirty_line;   /* Last dirty line */
    int frame_dirty;      /* Full frame dirty flag */
} CACHE_ALIGNED dirty_rect_t;

/**
 * Initialize dirty rectangle tracker
 */
static FORCE_INLINE void DIRTY_RECT_INIT(dirty_rect_t *dr)
{
    dr->frame_dirty = 1;
    dr->min_dirty_line = 0;
    dr->max_dirty_line = 287;
    for (int i = 0; i < 288; i++) {
        dr->dirty_lines[i] = 1;
    }
}

/**
 * Mark a scanline as dirty
 */
static FORCE_INLINE void DIRTY_RECT_MARK_LINE(dirty_rect_t *dr, int line)
{
    if (LIKELY(line >= 0 && line < 288)) {
        dr->dirty_lines[line] = 1;
        if (line < dr->min_dirty_line) dr->min_dirty_line = line;
        if (line > dr->max_dirty_line) dr->max_dirty_line = line;
    }
}

/**
 * Clear dirty rectangle tracker (after render)
 */
static FORCE_INLINE void DIRTY_RECT_CLEAR(dirty_rect_t *dr)
{
    dr->frame_dirty = 0;
    dr->min_dirty_line = 287;
    dr->max_dirty_line = 0;
    for (int i = 0; i < 288; i++) {
        dr->dirty_lines[i] = 0;
    }
}

/**
 * Check if a line needs rendering
 */
static FORCE_INLINE int DIRTY_RECT_IS_DIRTY(const dirty_rect_t *dr, int line)
{
    return dr->frame_dirty || dr->dirty_lines[line];
}

/**
 * Optimized palette update with bulk conversion
 * Updates TabPoints for faster rendering
 */
HOT_FUNCTION
static inline void CalcPoints_Optimized(core_crocods_t *core)
{
    const int mode = core->lastMode;
    
    /* Early exit if mode is invalid */
    if (UNLIKELY(mode < 0 || mode > 3)) {
        return;
    }
    
    /* Prefetch palette data */
    PREFETCH(core->BG_PALETTE);
    PREFETCH(core->TabCoul);
    
    /* Convert all 256 possible byte values for current mode */
    for (int i = 0; i < 256; i++) {
        /* Get 4 palette indices for this byte value */
        const u8 *pal_indices = core->TabPointsDef[mode][i];
        
        /* Convert to RGB565 and store in TabPoints */
        for (int j = 0; j < 4; j++) {
            const u8 pen = pal_indices[j];
            const u8 color = core->TabCoul[pen];
            core->TabPoints[mode][i][j] = core->BG_PALETTE[color];
        }
    }
    
    core->UpdateInk = 0;
}

#endif /* VIDEO_OPTIMIZED_H */
