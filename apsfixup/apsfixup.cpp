// SPDX-License-Identifier: Apache-2.0
//
// libapsfixup.so — OnePlus 12R / Ace 3 (aston / astonc, sm8550) APS turbo capture fix.
//
// FIRST-PARTY, source-built cc_library_shared (NOT a blob patch). It leaves the prebuilt
// /odm/lib64 OPlus algo blobs byte-identical and corrects the values they CONSUME at runtime,
// purely via GOT/PLT JUMP_SLOT redirects (mprotect RW -> overwrite the data pointer ->
// mprotect RO). No inline code patching, so it needs NEITHER execmem NOR execmod (both
// neverallow'd for app domains; libAlgoProcess.so runs inside the com.oplus.camera app process
// on the CapThread_* algo workers).
//
// ──────────────────────────────────────────────────────────────────────────────────────────
// Why this rewrite (kiro-45, 2026-06-16):
//   The minimal kiro-44 build hooked ONLY p010LSB2MSB and clamped its walk length. That stops
//   the over-READ inside p010LSB2MSB, but it does NOT fix the underlying defect: the port's
//   gralloc reports a wrong plane layout for the full-res P010 turbo output, so the byte-
//   identical ArcSoft/Algo blob computes a GARBAGE chroma (UV) plane pointer — a multi-GB
//   "gap" above luma (an align_up overflow). With p010 clamped, the crash simply MOVED one
//   frame up, to the full-plane copy in hwIPEDoProcessImpl:
//       #00 __memcpy_aarch64_simd+364           (SEGV, write)
//       #01 hwIPEDoProcessImpl+1776   x2(len)=0xb3e3ae00 (~3 GB, garbage plane size)
//   i.e. the blob memcpy's the (garbage-sized) plane to/from the garbage chroma pointer.
//
//   So the real fix is to correct the garbage CHROMA POINTER (and bound the p010 walk and the
//   NULL-RTB teardown), as the (no-crash) reference build did. That build was stable but left a
//   GREEN TINT, because it placed UV at `luma + (2/3 * mapping_size)`: when the dmabuf mapping
//   carries padding beyond the image, 2/3 of the *mapping* overshoots 2/3 of the *image*, so UV
//   lands past the real plane and the chroma comes back wrong (U=V≈0 -> green).
//
//   This version places UV at the geometrically exact luma-plane size `width*height*2`
//   (P010 = 2 bytes/sample), which is the natural contiguous 4:2:0 offset and is independent of
//   mapping padding. It falls back to the mapping-derived 2/3 only if the exact size doesn't fit.
//
// Corrections applied (all GOT redirection only):
//   (1) ARC_Turbo_RAW_Process output struct: chroma(UV) ptr = luma + width*height*2 (was a
//       multi-GB garbage gap)  -> fixes both the SIGSEGV plane-copy and the green tint.
//   (2) p010LSB2MSB: clamp the conversion length (w5) so the loop walk (w4*w5*1.5) fits the
//       mapped buffer (no over-read).
//  (2b) memcpy: clamp any plane copy whose length over-runs the src/dst mapping (kiro-46). The
//       (1) repair fixes the ARC output struct, but hwIPEDoProcessImpl copies from a SEPARATE
//       OfflineBufferInfos that still holds the garbage UV ptr, so its copy length comes out as
//       (0x7900000000 - plane_ptr) ~3-4 GB and walks off the buffer. Two tombstones (rear pid
//       17528 len=0xd7959ff0, night pid 17924 len=0xe4da2000, both == garbage_base - plane_ptr)
//       confirm it. Bounding the copy to the mapped bytes stops the SIGSEGV at the fault site.
//   (3) AlgoInterface::uninit: bypass when `this` is NULL (RTB init-fail teardown) -> no SIGSEGV.
//   (4) std::mutex lock/unlock/dtor: guard NULL+offset calls (defensive; ends the worker cleanly).
//
// ──────────────────────────────────────────────────────────────────────────────────────────
// sm8550 (astonc) offsets — verified against the DEPLOYED blobs (readelf -rW), DO NOT reuse
// the dodge/sm8850 values:
//   libAlgoProcess.so    BuildId 1d7e89c4c5ef30e12443e1e96059321c
//     p010LSB2MSB        GOT @ +0x703748  (func body +0x41efec)  _ZN18APSFormatConverter11p010LSB2MSBEPtS0_jjjj
//     memcpy@LIBC        GOT @ +0x7003f0
//     AlgoInterface::uninit GOT @ +0x700830                       _ZN7android13AlgoInterface6uninitEPv
//     std::mutex ~/lock/unlock GOT @ +0x700720 / +0x700728 / +0x700730
//   libAlgoInterface.so  BuildId a0f663b85c6eb794fcf4db20acd8b0c5
//     dlsym@LIBC         GOT @ +0x1c54ea8
//     camApsBufferLockPlanes GOT @ +0x1c55698  (UND import, resolved to libAlgoProcess's body)
//   Both libs are BIND_NOW (slots resolved at load -> GOT redirect is safe).
//
// ──────────────────────────────────────────────────────────────────────────────────────────
// kiro-52 (2026-06-17): the P010 ROTATE/MIRROR crash + the residual GREEN.
//   Tombstones in the 10-bit/HEIF path SIGSEGV inside libAlgoInterface.so:
//       #00 rotate_diffIO_v00<unsigned short,...>
//       #02 rotateP010(ApsBufferPlanes&, ApsBufferPlanes&, int)
//       #04 android::rotateMirrorProcess(AlgoProcessData*)   <- registered algo node
//       #05 APSCaptureModeManager::workRoutine               (libAlgoProcess.so)
//   reading/writing the garbage chroma 0x79000000xx. The (5) camlock geom fix DOES fire and
//   repairs the planes — but ONLY for libAlgoProcess.so's OWN camApsBufferLockPlanes call (the
//   slot we hooked is libAlgoProcess-local, CAMLOCK_BODY_OFF). libAlgoInterface.so ALSO imports
//   camApsBufferLockPlanes (its own JUMP_SLOT @ +0x1c55698) and locks the rotate/format-convert
//   ApsBufferPlanes through THAT slot, which was never redirected -> those planes keep the
//   garbage chroma -> rotateP010 walks off the end (crash) and the format conversions write to
//   the wrong/zero UV (green). FIX: redirect libAlgoInterface's camApsBufferLockPlanes slot to
//   the SAME wrap_camlock, so its locked planes get the identical geometry repair. rotateP010
//   was confirmed to read luma@+0x18 / luma_h@+0x24 / chroma@+0x48 / chr_h@+0x54 — i.e.
//   ApsBufferPlanes == the same struct wrap_camlock_fix already repairs.
//
#include <android/log.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define TAG "apsfixup"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)

// ── libAlgoProcess.so JUMP_SLOTs ───────────────────────────────────────────────────────────
static const uintptr_t P010_GOT_OFF   = 0x703748;  // APSFormatConverter::p010LSB2MSB
static const uintptr_t MEMCPY_GOT_OFF = 0x7003f0;  // memcpy@LIBC (hwIPEDoProcessImpl plane copy)
static const uintptr_t CAMLOCK_GOT_OFF = 0x700ad8; // camApsBufferLockPlanes (plane-layout source)
static const uintptr_t CAMLOCK_BODY_OFF = 0x1dbdd4;// local impl (drift guard for the JUMP_SLOT)
static const uintptr_t UNINIT_GOT_OFF = 0x700830;  // android::AlgoInterface::uninit(void*)
static const uintptr_t MUTEX_DTOR_GOT_OFF   = 0x700720;
static const uintptr_t MUTEX_LOCK_GOT_OFF   = 0x700728;
static const uintptr_t MUTEX_UNLOCK_GOT_OFF = 0x700730;
// ── libAlgoInterface.so JUMP_SLOTs ─────────────────────────────────────────────────────────
static const uintptr_t DLSYM_GOT_OFF  = 0x1c54ea8; // dlsym@LIBC
static const uintptr_t CAMLOCK_IFACE_GOT_OFF = 0x1c55698; // camApsBufferLockPlanes (UND import,
                                                          // resolved into libAlgoProcess.so)

// ── /proc/self/maps helpers ─────────────────────────────────────────────────────────────────
static bool range_of(uint64_t addr, uint64_t* out_base, uint64_t* out_size) {
    addr &= 0x00ffffffffffffffULL;                 // strip AArch64 top-byte tag (TBI/MTE)
    FILE* f = fopen("/proc/self/maps", "re");
    if (!f) return false;
    char line[512]; bool found = false;
    while (fgets(line, sizeof(line), f)) {
        uint64_t lo, hi;
        if (sscanf(line, "%" SCNx64 "-%" SCNx64, &lo, &hi) != 2) continue;
        if (addr >= lo && addr < hi) { *out_base = lo; *out_size = hi - lo; found = true; break; }
    }
    fclose(f);
    return found;
}
static bool module_base(const char* name, uint64_t* out_base) {
    FILE* f = fopen("/proc/self/maps", "re");
    if (!f) return false;
    char line[512]; uint64_t best = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, name)) {
            uint64_t lo;
            if (sscanf(line, "%" SCNx64, &lo) == 1) if (best == 0 || lo < best) best = lo;
        }
    }
    fclose(f);
    if (best) { *out_base = best; return true; }
    return false;
}

// True if `addr` falls inside any mapping whose line names `module`.
static bool addr_in_module(uint64_t addr, const char* module) {
    addr &= 0x00ffffffffffffffULL;                  // strip AArch64 top-byte tag
    FILE* f = fopen("/proc/self/maps", "re");
    if (!f) return false;
    char line[512]; bool found = false;
    while (fgets(line, sizeof(line), f)) {
        uint64_t lo, hi;
        if (sscanf(line, "%" SCNx64 "-%" SCNx64, &lo, &hi) != 2) continue;
        if (addr >= lo && addr < hi) { found = (strstr(line, module) != nullptr); break; }
    }
    fclose(f);
    return found;
}

// Overwrite a relro GOT slot (data, not code) -> no execmem/execmod.
static bool got_redirect(uint64_t slot, void* newval, void** old) {
    void** got = (void**)slot;
    uintptr_t page = slot & ~(uintptr_t)0xfff;
    if (mprotect((void*)page, 0x1000, PROT_READ | PROT_WRITE) != 0) {
        LOGW("mprotect GOT %p failed", (void*)slot);
        return false;
    }
    if (old) *old = *got;
    *got = newval;
    mprotect((void*)page, 0x1000, PROT_READ);       // restore relro (BIND_NOW: nothing else writes it)
    return true;
}

// ── (1) chroma-plane pointer repair ─────────────────────────────────────────────────────────
// The byte-identical Algo blob computes a garbage chroma (UV) pointer for the APS turbo output:
// a multi-GB bogus "gap" above the luma plane (an align_up overflow), NOT a real gralloc offset.
// Writing UV to that gap faults outright (the ~3 GB plane memcpy in hwIPEDoProcessImpl) and/or
// leaves the real UV plane unwritten (green tint). The real UV plane is contiguous, immediately
// after the Y plane. For P010 4:2:0 the Y plane is exactly width*height*2 bytes (2 bytes/sample);
// this is the natural contiguous offset and, unlike 2/3-of-mapping, is immune to dmabuf padding.
//
// Struct layout (astonc, runtime-verified): width @ +0x14, height @ +0x18,
// luma ptr @ +0x20, chroma ptr @ +0x28.
static void repair_struct(void* p) {
    if (!p) return;
    uint64_t mb, ms; if (!range_of((uint64_t)p, &mb, &ms)) return;
    uint8_t* b = (uint8_t*)p;

    uint32_t width  = *(uint32_t*)(b + 20);
    uint32_t height = *(uint32_t*)(b + 24);

    // Only act on plausible full-frame image structs.
    if (!(width > 1000 && width < 10000 && height > 1000 && height < 10000)) return;

    uint64_t luma = *(uint64_t*)(b + 32);
    if (luma == 0) return;
    uint64_t orig_chroma = *(uint64_t*)(b + 40);

    // A real contiguous UV plane sits a few MB above luma. The garbage pointer is a multi-GB
    // gap; use a 256 MB threshold to tell them apart (a no-op on already-correct buffers).
    if (!(orig_chroma > luma && (orig_chroma - luma) > (256ULL << 20))) return;

    uint64_t lb, ls;
    if (!range_of(luma, &lb, &ls)) return;
    uint64_t avail = (lb + ls) - luma;

    // Geometrically exact P010 luma-plane size. UV (4:2:0) needs another ysize/2 bytes.
    uint64_t ysize = (uint64_t)width * (uint64_t)height * 2ULL;
    if (ysize == 0 || ysize + (ysize / 2) > avail) {
        // Exact size doesn't fit the mapping (unexpected layout) -> safe mapping-derived fallback.
        ysize = (avail * 2 / 3) & ~0xfffULL;
    }
    *(uint64_t*)(b + 40) = luma + ysize;            // contiguous UV plane
    LOGI("chroma fix: %ux%u luma=%p garbage=%p -> %p (ysize=0x%llx avail=0x%llx)",
         width, height, (void*)luma, (void*)orig_chroma, (void*)(luma + ysize),
         (unsigned long long)ysize, (unsigned long long)avail);
}

// ARC_Turbo_RAW_Process takes x0-x7 PLUS ~7 stack args, so we CANNOT use a C wrapper (it would
// drop the stack args). Instead: a naked asm trampoline that repairs the 3 output structs
// (x1/x2/x3) then tail-branches to the real function with the FULL register+stack frame intact.
extern "C" __attribute__((visibility("hidden"))) void* aps_real_arc = nullptr;
extern "C" __attribute__((visibility("hidden"))) void aps_repair_structs(void* a1, void* a2, void* a3) {
    repair_struct(a1); repair_struct(a2); repair_struct(a3);
}
extern "C" void wrap_arc();   // defined in asm below; what we hand back from dlsym
__asm__(
"    .text\n"
"    .balign 4\n"
"    .global wrap_arc\n"
"    .type wrap_arc, %function\n"
"wrap_arc:\n"
"    stp x29, x30, [sp, #-0x60]!\n"   // our frame; sp moves DOWN, caller's stack args stay above
"    mov x29, sp\n"
"    stp x0, x1, [sp, #0x10]\n"       // save arg regs x0..x7
"    stp x2, x3, [sp, #0x20]\n"
"    stp x4, x5, [sp, #0x30]\n"
"    stp x6, x7, [sp, #0x40]\n"
"    ldr x0, [sp, #0x18]\n"           // aps_repair_structs(orig x1, orig x2, orig x3)
"    ldr x1, [sp, #0x20]\n"
"    ldr x2, [sp, #0x28]\n"
"    bl  aps_repair_structs\n"
"    ldp x0, x1, [sp, #0x10]\n"       // restore arg regs
"    ldp x2, x3, [sp, #0x20]\n"
"    ldp x4, x5, [sp, #0x30]\n"
"    ldp x6, x7, [sp, #0x40]\n"
"    ldp x29, x30, [sp], #0x60\n"     // pop frame -> sp back to entry (stack args in place), x30 restored
"    adrp x16, aps_real_arc\n"
"    add  x16, x16, #:lo12:aps_real_arc\n"
"    ldr  x16, [x16]\n"
"    br   x16\n"                      // tail-call real ARC; it returns straight to the caller
);

// ── dlsym interposer in libAlgoInterface: swap ARC_Turbo_RAW_Process for our wrapper ─────────
typedef void* (*dlsym_t)(void*, const char*);
static dlsym_t g_real_dlsym = nullptr;
static void* wrap_dlsym(void* handle, const char* symbol) {
    // OCamTurboHdr queries V3/V2 init variants this blob does not export; alias to the base
    // ARC_Turbo_RAW_Init so turbo HDR init succeeds (else "find ARC_Turbo_RAW_Init_V3 failed").
    if (symbol && (strcmp(symbol, "ARC_Turbo_RAW_Init_V3") == 0 ||
                   strcmp(symbol, "ARC_Turbo_RAW_Init_V2") == 0 ||
                   strcmp(symbol, "ARC_Turbo_RAW_InitV2") == 0)) {
        LOGI("aliasing '%s' -> ARC_Turbo_RAW_Init", symbol);
        return g_real_dlsym(handle, "ARC_Turbo_RAW_Init");
    }
    void* res = g_real_dlsym(handle, symbol);
    if (symbol && res && strcmp(symbol, "ARC_Turbo_RAW_Process") == 0) {
        aps_real_arc = res;
        LOGI("interposing ARC_Turbo_RAW_Process (real=%p)", res);
        return (void*)wrap_arc;
    }
    return res;
}

// ── per-thread geometry cache filled by wrap_camlock_fix ──────────────────────────────────────
struct chroma_geom {
    uint64_t luma_ptr;       // source luma base (the output buffer)
    uint64_t chroma_ptr;     // correct chroma source = luma + luma_bytes
    uint32_t chr_stride;     // chroma byte pitch
    uint32_t chr_h;          // chroma height (== luma_height for contiguous 4:2:0)
    uint64_t luma_bytes;     // luma plane total bytes (for dst-side chroma offset)
};
// Up to 4 concurrent capture threads; linear scan is fine for this count.
static constexpr int MAX_GEOM = 4;
static struct { pthread_t tid; chroma_geom g; } g_geom_cache[MAX_GEOM];
static pthread_mutex_t g_geom_mtx = PTHREAD_MUTEX_INITIALIZER;

static void geom_cache_store(const chroma_geom& g) {
    pthread_t self = pthread_self();
    pthread_mutex_lock(&g_geom_mtx);
    // Update existing slot for this thread, or use an empty one, or evict oldest (slot 0).
    int slot = -1;
    for (int i = 0; i < MAX_GEOM; i++) {
        if (g_geom_cache[i].tid == self || g_geom_cache[i].g.luma_ptr == 0) { slot = i; break; }
    }
    if (slot < 0) slot = 0;  // evict
    g_geom_cache[slot].tid = self;
    g_geom_cache[slot].g = g;
    pthread_mutex_unlock(&g_geom_mtx);
}

// Look up cached geometry by matching the garbage src to a known output buffer's garbage chroma.
// Also try matching by dst address (dst == some luma_dst + luma_bytes).
static bool geom_cache_lookup_by_src(uint64_t src, chroma_geom* out) {
    pthread_mutex_lock(&g_geom_mtx);
    for (int i = 0; i < MAX_GEOM; i++) {
        if (g_geom_cache[i].g.luma_ptr == 0) continue;
        // The garbage src should NOT match the corrected chroma_ptr. But we want to inject whenever
        // src is garbage (and we have geometry for a matching-sized buffer). Accept any slot whose
        // geometry is populated — the IPE operates on the most-recently-locked output buffer, and
        // there's only one capture in flight per thread.
        *out = g_geom_cache[i].g;
        pthread_mutex_unlock(&g_geom_mtx);
        return true;
    }
    pthread_mutex_unlock(&g_geom_mtx);
    return false;
}

// A "garbage chroma" VA: high 32 bits in [0x60,0x7f], low 32 bits < 0x100000 (align_up(luma,0)
// pattern — a valid buffer always has a non-trivial page offset).
static inline bool is_garbage_va(uint64_t v) {
    uint32_t hi = (uint32_t)(v >> 32);
    uint32_t lo = (uint32_t)(v & 0xffffffffULL);
    return hi >= 0x60 && hi <= 0x7f && lo < 0x100000;
}


// ── (2) p010LSB2MSB length clamp ─────────────────────────────────────────────────────────────
// The body reads the FIRST arg and writes the SECOND (in-place at the live call site) and walks
// ~1.5*w4*w5 bytes (w4 = width, w5 = luma height). If that exceeds the mapped buffer, shrink w5.
typedef void (*p010_t)(uint16_t*, uint16_t*, uint32_t, uint32_t, uint32_t, uint32_t);
static p010_t g_real_p010 = nullptr;
static void wrap_p010(uint16_t* dst, uint16_t* src, uint32_t w2, uint32_t w3, uint32_t w4, uint32_t w5) {
    uint64_t s = (uint64_t)src & 0x00ffffffffffffffULL;

    // kiro-50: if src is a garbage chroma VA, substitute the correct source from the geom cache.
    // The 10-bit path calls p010LSB2MSB with src=garbage_chroma → SIGSEGV. Redirect to correct UV.
    if (is_garbage_va(s)) {
        chroma_geom cg;
        if (geom_cache_lookup_by_src(s, &cg) && cg.chroma_ptr) {
            LOGI("p010 chroma redirect: src=%p(garbage)->%p", src, (void*)cg.chroma_ptr);
            src = (uint16_t*)cg.chroma_ptr;
        } else {
            LOGI("p010 skip: src=%p is garbage chroma, no cached geometry -> skip (prevent crash)",
                 src);
            return;  // skip rather than SIGSEGV
        }
    }

    if (w4 > 0) {
        uint64_t sb, ss, db, ds;
        if (range_of((uint64_t)src, &sb, &ss) && range_of((uint64_t)dst, &db, &ds)) {
            uint64_t src_avail = (sb + ss) - ((uint64_t)src & 0x00ffffffffffffffULL);
            uint64_t dst_avail = (db + ds) - ((uint64_t)dst & 0x00ffffffffffffffULL);
            uint64_t avail = (src_avail < dst_avail) ? src_avail : dst_avail;
            uint64_t walk  = (uint64_t)w4 * w5 * 3 / 2;            // bytes the loop touches
            if (walk > avail) {
                uint32_t luma_w5 = (uint32_t)((avail * 2 / 3) / w4);   // 1.5*w4*luma_w5 == avail
                if (luma_w5 > 0 && luma_w5 < w5) {
                    LOGI("p010 over-walk fix: w5 %u -> %u (w4=%u walk=0x%llx avail=0x%llx)",
                         w5, luma_w5, w4, (unsigned long long)walk, (unsigned long long)avail);
                    g_real_p010(dst, src, w2, w3, w4, luma_w5);
                    return;
                }
            }
        }
    }
    g_real_p010(dst, src, w2, w3, w4, w5);
}

// ── (2b) memcpy plane-copy clamp + chroma injection ─────────────────────────────────────────
// HISTORY: kiro-46 added a pure length clamp (n >= 16 MB → bound to mapping). That catches the
// SIGSEGV (garbage-length luma over-copy). But the GREEN TINT root cause is different:
// hwIPEDoProcessImpl reads plane layout from an INTERNAL struct (OfflineBufferInfos/AlgoProcessData)
// that neither the ARC wrapper nor the camlock wrapper can reach. Its chroma copy fires as
// memcpy(dst_chroma, GARBAGE_src, 0) — length ZERO because chroma_height is 0 in that struct.
// Zero-length memcpy is a no-op in libc, so UV is never written → green.
//
// kiro-50 FIX: the camlock wrapper already resolves the correct geometry for every output buffer.
// We cache the last-seen output buffer's luma base + plane dimensions per-thread. Then in
// wrap_memcpy we detect the defective chroma copy by its signature:
//   (a) src is a "garbage chroma" pointer (hi 0x60–0x7f, lo < 0x100000) — this is the align_up
//       overflow the blob computes, OR
//   (b) n == 0 AND dst is the expected chroma-dst address (luma_dst + luma_bytes)
// When detected, we substitute the correct src (luma + luma_bytes from the real mapping) and
// length (chr_stride * chr_h). This injects the missing chroma copy at the fault site and is
// self-checking: only fires when src matches the garbage pattern that no real buffer ever has.
//
// For the OVER-SIZE case (n >= 16 MB, garbage luma stride producing a multi-GB length), the
// original kiro-46 clamp is retained as a backstop.

typedef void* (*memcpy_t)(void*, const void*, size_t);
static memcpy_t g_real_memcpy = nullptr;
static void* wrap_memcpy(void* dst, const void* src, size_t n) {
    uint64_t s = (uint64_t)src & 0x00ffffffffffffffULL;

    // ── (2b-inject) kiro-50: detect the defective chroma copy and INJECT correct data ────────
    // Signature: src is a garbage VA (hi 0x60-0x7f, lo < 0x100000) AND n is tiny (0 or < 4 KB).
    // A real multi-MB plane copy never has src with lo < 0x100000, so this is safe.
    if (is_garbage_va(s) && n < 4096) {
        chroma_geom cg;
        if (geom_cache_lookup_by_src(s, &cg) && cg.chroma_ptr && cg.chr_stride && cg.chr_h) {
            size_t correct_len = (size_t)cg.chr_stride * cg.chr_h;
            // Validate the correct source is mapped and has enough bytes.
            uint64_t cb, cs;
            uint64_t cptr = cg.chroma_ptr & 0x00ffffffffffffffULL;
            if (correct_len > 0 && range_of(cptr, &cb, &cs)) {
                uint64_t c_avail = (cb + cs) - cptr;
                if (correct_len <= c_avail) {
                    // Also validate dst is mapped and has enough space.
                    uint64_t d = (uint64_t)dst & 0x00ffffffffffffffULL;
                    uint64_t db, ds2;
                    if (range_of(d, &db, &ds2)) {
                        uint64_t d_avail = (db + ds2) - d;
                        if (correct_len <= d_avail) {
                            LOGI("memcpy chroma inject: dst=%p src=%p(garbage)->%p n=%zu->%zu",
                                 dst, src, (void*)cg.chroma_ptr,
                                 n, correct_len);
                            return g_real_memcpy(dst, (const void*)cg.chroma_ptr, correct_len);
                        }
                    }
                }
            }
        }
    }

    // ── (2b-clamp) kiro-46 original: bound over-sized copies to the mapping ──────────────────
    if (n >= (16ULL << 20)) {
        uint64_t d = (uint64_t)dst & 0x00ffffffffffffffULL;
        uint64_t db, ds2, sb, ss;
        if (range_of(d, &db, &ds2) && range_of(s, &sb, &ss)) {
            uint64_t dst_avail = (db + ds2) - d;
            uint64_t src_avail = (sb + ss) - s;
            uint64_t avail = (src_avail < dst_avail) ? src_avail : dst_avail;
            if (n > avail) {
                LOGI("memcpy over-copy fix: n=0x%llx -> 0x%llx (dst=%p src=%p)",
                     (unsigned long long)n, (unsigned long long)avail, dst, src);
                n = (size_t)avail;
            }
        }
    }

    return g_real_memcpy(dst, src, n);
}
typedef int (*uninit_t)(void*, void*);
static uninit_t g_real_uninit = nullptr;
static int wrap_uninit(void* thiz, void* arg) {
    if (!thiz) {
        LOGI("uninit fix: bypass AlgoInterface::uninit with NULL this");
        return 0;
    }
    return g_real_uninit(thiz, arg);
}

// ── (5) camApsBufferLockPlanes plane-layout rebuild — the GREEN-TINT / crash ROOT ────────────
// EVIDENCE (kiro-49, geometry-probe log 2026-06-16 23:18, pid 9379, ALGO_TURBO_HDR_NIGHT 4096x3072
// 8-bit, "saved but GREEN"): the kiro-48 DUMP resolved the struct field offsets AND the true
// geometry of the defective APS turbo OUTPUT buffer (gralloc format 0x7fa30c0a — a CONTIGUOUS
// 4:2:0 P010-turbo buffer, one mapping = luma plane then chroma plane, avail=0x2400000):
//     +0x08 width   +0x18 luma_ptr   +0x24 luma_height   +0x28 luma_stride(bytes)
//     +0x48 chroma_ptr   +0x54 chroma_height   +0x58 chroma_stride(bytes)
// On the port the lock returns luma_ptr(+0x18) and luma_height(+0x24=8192) VALID but three fields
// garbage:  luma_stride(+0x28)=0xfc7bf3d6(~4.2 GB),  chroma_ptr(+0x48)=0x7100000000(multi-GB gap),
// chroma_height(+0x54)=0.  hwIPEDoProcessImpl @0x6841c4 does a SPLIT copy whose lengths are
// luma_stride*luma_height (explodes -> the over-walk the (2)/(2b) clamps catch) and
// chroma_stride*chroma_height (= N*0 = 0 -> UV never written -> GREEN).
//
// The kiro-47 attempt (luma_stride = (chroma_ptr - luma_ptr)/luma_height) was INERT — chroma_ptr
// is ALSO garbage. kiro-49 instead rebuilds from the one fully-trustworthy quantity, the kernel
// mapping size: for a contiguous 4:2:0 buffer avail splits luma=2/3, chroma=1/3, and luma_height
// is uncorrupted, so luma_stride=(avail*2/3)/luma_height, chroma_ptr=luma+avail*2/3,
// chroma_height=luma_height, chroma_stride=(avail/3)/luma_height. (4096x3072: avail 0x2400000 ->
// luma 0x1800000 stride 3072, chroma 0xC00000 @ luma+0x1800000 stride 1536.) This corrects BOTH
// the copy length and the plane offset/length at the source; the (2)/(2b) clamps become pure
// backstops. Self-checking: bails (leaving the buffer untouched) on any non-clean / not-fully-
// mapped layout, and never matches sane preview (fmt 0x11) or single-plane RAW (fmt 0x20) buffers.
typedef int (*camlock_t)(void*, void*, int, int, void*);
static camlock_t g_real_camlock = nullptr;
static void wrap_camlock_fix(void* info) {
    if (!info) return;
    uint8_t* s = (uint8_t*)info;
    uint32_t format = *(uint32_t*)(s + 0x00);
    uint32_t width  = *(uint32_t*)(s + 0x08);   // width (samples)
    uint64_t luma   = *(uint64_t*)(s + 0x18);   // luma plane base
    uint32_t luma_h = *(uint32_t*)(s + 0x24);   // luma height (scanlines) — uncorrupted
    uint32_t luma_s = *(uint32_t*)(s + 0x28);   // luma stride (bytes)     — garbage on the defect
    uint64_t chroma = *(uint64_t*)(s + 0x48);   // chroma plane base       — garbage on the defect
    uint32_t chr_h  = *(uint32_t*)(s + 0x54);   // chroma height           — 0 on the defect
    uint32_t chr_s  = *(uint32_t*)(s + 0x58);   // chroma stride (bytes)
    LOGI("camlock planes: fmt=0x%x w=%u luma=%p h=%u stride=%u | chroma=%p h=%u stride=%u",
         format, width, (void*)luma, luma_h, luma_s, (void*)chroma, chr_h, chr_s);

    // ── (5) kiro-49: rebuild the whole plane geometry from the mapping (the GREEN/crash fix) ────
    // The kiro-48 probe (camlock DUMP, raw struct words) RESOLVED the field offsets and the true
    // geometry. The defective APS turbo OUTPUT buffer is fmt=0x7fa30c0a, a CONTIGUOUS 4:2:0
    // P010-turbo buffer (one mapping holds luma then chroma). On the port its locked struct comes
    // back with THREE garbage fields while luma_ptr(+0x18) and luma_height(+0x24) stay VALID:
    //   +0x28 luma_stride = 0xfc7bf3d6  (~4.2 GB)   -> should be 3072
    //   +0x48 chroma_ptr  = 0x7100000000 (multi-GB gap) -> should be luma + luma_plane_bytes
    //   +0x54 chroma_height = 0                     -> should be luma_height (== 8192 here)
    //   (+0x58 chroma_stride = 1536 is already correct)
    // hwIPEDoProcessImpl does a SPLIT copy: luma memcpy(dst, luma, luma_stride*luma_height) then
    // chroma memcpy(dst + luma_height*align_up(width), chroma_ptr, chroma_stride*chroma_height).
    // With chroma_height==0 the chroma copy is ZERO bytes -> UV plane never written -> GREEN; the
    // garbage luma_stride explodes the luma length -> the over-walk the (2)/(2b) clamps catch.
    //
    // The kiro-47 fix (luma_stride = (chroma_ptr-luma)/luma_height) was INERT: chroma_ptr is ALSO
    // garbage, so it could never derive the pitch. Instead rebuild from the one fully-trustworthy
    // source — the kernel mapping size. For contiguous 4:2:0, avail = luma + chroma with
    // luma = 2/3 avail and chroma = 1/3 avail; luma_height (+0x24) is uncorrupted, so:
    //   luma_stride   = (avail*2/3) / luma_height          (= 0x1800000/8192 = 3072)
    //   chroma_ptr    = luma + avail*2/3                   (contiguous, immediately after luma)
    //   chroma_height = luma_height                        (this rep stores both planes "tall")
    //   chroma_stride = (avail/3) / luma_height            (= 0xC00000/8192 = 1536)
    // This corrects BOTH the luma copy length and the chroma plane (offset+length) at the source,
    // making the (2)/(2b) clamps pure backstops. Bounded & self-checking: it bails (leaving the
    // buffer untouched) on anything that is not a clean, fully-mapped contiguous 4:2:0 layout, so
    // it can never turn a working buffer green or crash it.

    // Defect signature: a garbage (multi-GB) luma stride, OR a garbage (multi-GB gap) chroma ptr.
    // Sane preview (fmt 0x11) and single-plane RAW (fmt 0x20, chroma==0, luma fills the mapping)
    // buffers do NOT match and are left untouched.
    bool stride_garbage = (luma_s > (1u << 20));
    bool chroma_gap_garbage = (chroma > luma) && ((chroma - luma) > (256ULL << 20));
    if (!stride_garbage && !chroma_gap_garbage) return;

    if (luma == 0 || luma_h < 16 || luma_h > 100000) return;
    uint64_t lb, ls;
    if (!range_of(luma, &lb, &ls)) return;
    uint64_t avail = (lb + ls) - (luma & 0x00ffffffffffffffULL);

    // Require a clean contiguous 4:2:0 mapping: total = 3 units, luma = 2, chroma = 1, and each
    // plane an exact multiple of the (trusted) luma height. Bail otherwise — never blind-guess.
    if (avail < (1ULL << 20) || avail % 3 != 0) return;
    uint64_t luma_bytes = avail / 3 * 2;
    uint64_t chr_bytes  = avail - luma_bytes;
    if (luma_bytes % luma_h != 0 || chr_bytes % luma_h != 0) return;
    uint64_t l_stride = luma_bytes / luma_h;          // true luma byte pitch
    uint64_t c_stride = chr_bytes  / luma_h;          // true chroma byte pitch
    uint64_t new_chroma = luma + luma_bytes;          // contiguous UV plane
    if (l_stride == 0 || l_stride > (1u << 20) || c_stride == 0 || c_stride > (1u << 20)) return;

    LOGI("camlock geom fix: luma_s %u->%llu | chroma %p->%p chr_h %u->%u chr_s %u->%llu "
         "(avail=0x%llx luma_h=%u)",
         luma_s, (unsigned long long)l_stride, (void*)chroma, (void*)new_chroma,
         chr_h, luma_h, chr_s, (unsigned long long)c_stride,
         (unsigned long long)avail, luma_h);
    *(uint32_t*)(s + 0x28) = (uint32_t)l_stride;      // luma stride  (was ~4.2 GB garbage)
    *(uint64_t*)(s + 0x48) = new_chroma;              // chroma ptr   (was multi-GB gap)
    *(uint32_t*)(s + 0x54) = luma_h;                  // chroma height(was 0 -> zero-len -> green)
    *(uint32_t*)(s + 0x58) = (uint32_t)c_stride;      // chroma stride(idempotent if already right)

    // Cache geometry for wrap_memcpy's chroma injection (kiro-50).
    chroma_geom cg;
    cg.luma_ptr = luma;
    cg.chroma_ptr = new_chroma;
    cg.chr_stride = (uint32_t)c_stride;
    cg.chr_h = luma_h;
    cg.luma_bytes = luma_bytes;
    geom_cache_store(cg);
}
static int wrap_camlock(void* handle, void* info, int a2, int a3, void* a4) {
    int rc = g_real_camlock(handle, info, a2, a3, a4);
    wrap_camlock_fix(info);
    return rc;
}

// ── (4) std::mutex NULL+offset guards (defensive) ────────────────────────────────────────────
typedef void (*mutex_op_t)(void*);
static mutex_op_t g_real_mutex_lock = nullptr;
static mutex_op_t g_real_mutex_unlock = nullptr;
static mutex_op_t g_real_mutex_dtor = nullptr;
static void wrap_mutex_lock(void* m) {
    if ((uintptr_t)m < 0x10000) { LOGI("mutex fix: bypass lock on %p; ending worker", m); pthread_exit(NULL); }
    g_real_mutex_lock(m);
}
static void wrap_mutex_unlock(void* m) {
    if ((uintptr_t)m < 0x10000) { LOGI("mutex fix: bypass unlock on %p; ending worker", m); pthread_exit(NULL); }
    g_real_mutex_unlock(m);
}
static void wrap_mutex_dtor(void* m) {
    if ((uintptr_t)m < 0x10000) { LOGI("mutex fix: bypass dtor on %p; ending worker", m); pthread_exit(NULL); }
    g_real_mutex_dtor(m);
}

// ── install ──────────────────────────────────────────────────────────────────────────────────
static bool g_p010_done = false, g_dlsym_done = false, g_uninit_done = false;
static bool g_camlock_iface_done = false;   // libAlgoInterface's own camApsBufferLockPlanes slot
static void try_install() {
    uint64_t base;
    if ((!g_p010_done || !g_uninit_done) && module_base("libAlgoProcess.so", &base)) {
        void* old = nullptr;
        if (!g_p010_done) {
            void* expected = (void*)(base + 0x41efec);          // p010LSB2MSB body (drift guard)
            void** slot = (void**)(base + P010_GOT_OFF);
            if (*slot == expected && got_redirect(base + P010_GOT_OFF, (void*)wrap_p010, &old)) {
                g_real_p010 = (p010_t)old;
                LOGI("hooked p010LSB2MSB GOT @%p (real=%p)", (void*)(base + P010_GOT_OFF), old);
                g_p010_done = true;
            } else if (*slot != expected) {
                LOGW("p010 GOT @%p = %p, expected %p (lazy/drift?)",
                     (void*)(base + P010_GOT_OFF), *slot, expected);
            }
        }
        if (!g_uninit_done && got_redirect(base + UNINIT_GOT_OFF, (void*)wrap_uninit, &old)) {
            g_real_uninit = (uninit_t)old;
            LOGI("hooked AlgoInterface::uninit GOT (real=%p)", old);
            g_uninit_done = true;
        }
        if (!g_real_memcpy) {
            // Sanity: only redirect once the BIND_NOW slot is resolved into libc (not a lazy stub).
            void** slot = (void**)(base + MEMCPY_GOT_OFF);
            if (addr_in_module((uint64_t)*slot, "libc.so") &&
                got_redirect(base + MEMCPY_GOT_OFF, (void*)wrap_memcpy, &old)) {
                g_real_memcpy = (memcpy_t)old;
                LOGI("hooked memcpy GOT @%p (real=%p)", (void*)(base + MEMCPY_GOT_OFF), old);
            }
        }
        if (!g_real_camlock) {
            // Local JUMP_SLOT -> verify it resolves to this lib's own camApsBufferLockPlanes body.
            void** slot = (void**)(base + CAMLOCK_GOT_OFF);
            void* expected = (void*)(base + CAMLOCK_BODY_OFF);
            if (*slot == expected &&
                got_redirect(base + CAMLOCK_GOT_OFF, (void*)wrap_camlock, &old)) {
                g_real_camlock = (camlock_t)old;
                LOGI("hooked camApsBufferLockPlanes GOT @%p (real=%p)",
                     (void*)(base + CAMLOCK_GOT_OFF), old);
            } else if (*slot != expected) {
                LOGW("camlock GOT @%p = %p, expected %p (lazy/drift?)",
                     (void*)(base + CAMLOCK_GOT_OFF), *slot, expected);
            }
        }
        if (!g_real_mutex_lock && got_redirect(base + MUTEX_LOCK_GOT_OFF, (void*)wrap_mutex_lock, &old))
            g_real_mutex_lock = (mutex_op_t)old;
        if (!g_real_mutex_unlock && got_redirect(base + MUTEX_UNLOCK_GOT_OFF, (void*)wrap_mutex_unlock, &old))
            g_real_mutex_unlock = (mutex_op_t)old;
        if (!g_real_mutex_dtor && got_redirect(base + MUTEX_DTOR_GOT_OFF, (void*)wrap_mutex_dtor, &old))
            g_real_mutex_dtor = (mutex_op_t)old;
    }
    if ((!g_dlsym_done || !g_camlock_iface_done) && module_base("libAlgoInterface.so", &base)) {
        void* old = nullptr;
        if (!g_dlsym_done && got_redirect(base + DLSYM_GOT_OFF, (void*)wrap_dlsym, &old)) {
            g_real_dlsym = (dlsym_t)old;
            LOGI("hooked dlsym GOT in libAlgoInterface (real=%p)", old);
            g_dlsym_done = true;
        }
        // kiro-52: libAlgoInterface imports camApsBufferLockPlanes via its OWN JUMP_SLOT and
        // locks the rotate/format-convert planes through it. Redirect that slot to the same
        // wrap_camlock so those planes get the identical geometry repair (stops the rotateP010
        // SIGSEGV and the format-convert green). The slot is an UND import resolved (BIND_NOW)
        // into libAlgoProcess.so — verify that before redirecting (not a lazy PLT stub).
        if (!g_camlock_iface_done) {
            void** slot = (void**)(base + CAMLOCK_IFACE_GOT_OFF);
            if (addr_in_module((uint64_t)*slot, "libAlgoProcess.so") &&
                got_redirect(base + CAMLOCK_IFACE_GOT_OFF, (void*)wrap_camlock, &old)) {
                if (!g_real_camlock) g_real_camlock = (camlock_t)old;   // same real fn as the
                                                                        // libAlgoProcess slot
                LOGI("hooked camApsBufferLockPlanes GOT in libAlgoInterface @%p (real=%p)",
                     (void*)(base + CAMLOCK_IFACE_GOT_OFF), old);
                g_camlock_iface_done = true;
            }
        }
    }
}
static void* poller(void*) {
    // 25ms cadence; dlsym(ARC) happens at the first turbo capture (seconds after libAlgoInterface
    // loads), so this hooks well before it. ~10 min total budget.
    for (int i = 0; i < 24000 &&
         !(g_p010_done && g_dlsym_done && g_uninit_done && g_camlock_iface_done); i++) {
        try_install();
        usleep(25 * 1000);
    }
    if (!(g_p010_done && g_dlsym_done && g_uninit_done && g_camlock_iface_done))
        LOGW("install incomplete: p010=%d dlsym=%d uninit=%d camlock_iface=%d",
             g_p010_done, g_dlsym_done, g_uninit_done, g_camlock_iface_done);
    return nullptr;
}

__attribute__((constructor)) static void apsfixup_init() {
    LOGI("libapsfixup loaded (sm8550 APS turbo chroma/over-walk fix, pid %d)", getpid());
    try_install();                       // libAlgoProcess is loaded with us; libAlgoInterface may be too
    if (!(g_p010_done && g_dlsym_done && g_uninit_done && g_camlock_iface_done)) {
        pthread_t t;
        if (pthread_create(&t, nullptr, poller, nullptr) == 0) pthread_detach(t);
    }
}
