/**
 * @file z80_optimized.h
 * @brief Z80 optimization macros for ARM Cortex-A7 (Miyoo Mini)
 * 
 * This file provides compiler hints and macros to optimize the Z80 emulation
 * for low-power ARM hardware with small caches and weak branch prediction.
 * 
 * Optimizations:
 * - Branch prediction hints (LIKELY/UNLIKELY)
 * - Force inline for hot functions
 * - Cache prefetch hints
 * - Register variables for frequently accessed data
 */

#ifndef Z80_OPTIMIZED_H
#define Z80_OPTIMIZED_H

/* Branch prediction hints for ARM */
#if defined(__GNUC__) || defined(__clang__)
  #define LIKELY(x)       __builtin_expect(!!(x), 1)
  #define UNLIKELY(x)     __builtin_expect(!!(x), 0)
  #define FORCE_INLINE    __attribute__((always_inline)) inline
  #define HOT_FUNCTION    __attribute__((hot))
  #define COLD_FUNCTION   __attribute__((cold))
  #define PREFETCH(addr)  __builtin_prefetch((addr), 0, 3)
  #define PREFETCH_W(addr) __builtin_prefetch((addr), 1, 3)
#else
  #define LIKELY(x)       (x)
  #define UNLIKELY(x)     (x)
  #define FORCE_INLINE    inline
  #define HOT_FUNCTION
  #define COLD_FUNCTION
  #define PREFETCH(addr)
  #define PREFETCH_W(addr)
#endif

/* Cache alignment for critical structures */
#if defined(__GNUC__) || defined(__clang__)
  #define CACHE_ALIGNED __attribute__((aligned(64)))
#else
  #define CACHE_ALIGNED
#endif

/* Register hints for frequently accessed variables */
#if defined(__GNUC__) || defined(__clang__)
  #define REGISTER_HINT register
#else
  #define REGISTER_HINT
#endif

/**
 * PEEK8 - Force-inlined 8-bit memory read
 * Optimizations:
 * - Forced inlining to eliminate call overhead
 * - Direct array access with pre-computed table lookup
 * - No bounds checking (handled by TabPEEK structure)
 */
static FORCE_INLINE u8 PEEK8_OPT(core_crocods_t *core, u16 adr)
{
    /* Split address into page (top 2 bits) and offset (bottom 14 bits) */
    const u16 page = adr >> 14;
    const u16 offset = adr & 0x3FFF; /* MASK_14BIT */
    return core->TabPEEK[page][offset];
}

/**
 * POKE8 - Force-inlined 8-bit memory write
 */
static FORCE_INLINE void POKE8_OPT(core_crocods_t *core, u16 adr, u8 val)
{
    const u16 page = adr >> 14;
    const u16 offset = adr & 0x3FFF;
    core->TabPOKE[page][offset] = val;
}

/**
 * PEEK16 - Force-inlined 16-bit memory read (little-endian)
 * Optimizations:
 * - Pre-compute page/offset once
 * - Explicit little-endian byte ordering
 */
static FORCE_INLINE u16 PEEK16_OPT(core_crocods_t *core, u16 adr)
{
    const u16 page = adr >> 14;
    const u16 offset = adr & 0x3FFF;
    const u8 low = core->TabPEEK[page][offset];
    const u8 high = core->TabPEEK[page][offset + 1];
    return (u16)(low | (high << 8));
}

/**
 * POKE16 - Force-inlined 16-bit memory write (little-endian)
 */
static FORCE_INLINE void POKE16_OPT(core_crocods_t *core, u16 adr, u16 val)
{
    const u16 page = adr >> 14;
    const u16 offset = adr & 0x3FFF;
    core->TabPOKE[page][offset] = (u8)val;
    core->TabPOKE[page][offset + 1] = (u8)(val >> 8);
}

/**
 * Z80_FETCH_OPCODE - Optimized opcode fetch with prefetch hint
 * Fetches next opcode and increments PC, with cache prefetch for next instruction
 */
static FORCE_INLINE u8 Z80_FETCH_OPCODE(core_crocods_t *core, u16 *pc)
{
    const u16 current_pc = *pc;
    (*pc)++;
    
    /* Prefetch next instruction to reduce cache miss */
    PREFETCH(&core->TabPEEK[(*pc) >> 14][(*pc) & 0x3FFF]);
    
    return PEEK8_OPT(core, current_pc);
}

/**
 * Z80_UPDATE_R - Optimized R register update
 * Updates R register (lower 7 bits increment, bit 7 preserved)
 */
static FORCE_INLINE void Z80_UPDATE_R(u8 *reg_r)
{
    *reg_r = ((*reg_r + 1) & 0x7F) | (*reg_r & 0x80);
}

#endif /* Z80_OPTIMIZED_H */
