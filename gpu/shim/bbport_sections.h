// SPDX-License-Identifier: GPL-2.0-or-later
// bbport BB_SECTIONS=1 (diagnostics): time the draw recording thread spends per part of a draw,
// inclusive (nested parts count in their parent too), printed with the draw pipe statistics.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <x86intrin.h>

namespace BbSections {
enum Id : std::uint32_t {
    Packet,          ///< a whole packet of the draw pipe
    Task,            ///< ordered tasks (fences, signals, DMA, flips)
    DrawRecord,
    PrepareRenderState,
    BindResources,
    BindBuffers,
    BindTextures,
    BeginRendering,
    ResolveVertexBuffers,
    ResolveIndexBuffer,
    EmitVertexBuffers,
    UpdateDynamicState,
    DispatchRecord,
    ShaderHle,
    ObtainBuffer,
    Flush,
    Motion,          ///< object motion (bone palettes, index ranges)
    PipelineBind,    ///< Pipeline::BindResources (descriptors, push constants)
    Kick,            ///< ResetBindings and the hand-over to the Vulkan recording threads
    Preupload,
    EnsureResident,
    ObtainStream,  ///< ObtainBuffer's copy of a small read-only buffer into the stream buffer
    ObtainVram,    ///< ObtainBuffer's VRAM path (SynchronizeMemory)
    TexSet,        ///< BindTexturesFromSet: the memoized set, lookup to descriptors
    TexSetCheck,   ///< its check that every image of the set is still current
    TexSetApply,   ///< its per-image bookkeeping and layout transitions
    TexFull,       ///< BindTextures when the set did not apply: the full lookup
    Samplers,      ///< BindSamplers
    VramImage,     ///< SynchronizeMemoryFromImage (texel buffers over image memory)
    VramWritten,   ///< ObtainBuffer's bookkeeping for a GPU-written buffer
    TexFullEmpty,  ///< BindTextures for a stage without images
    RefreshHash,   ///< RefreshImage: guest memory hashes
    RefreshObtain, ///< RefreshImage: ObtainBufferForImage (the buffer side of the source)
    RefreshDetile, ///< RefreshImage: DetileImage
    RefreshUpload, ///< RefreshImage: UploadImage (recording the copy)
    Upscaler,      ///< TemporalUpscaler::Run: the upscaler's passes
    UpscalerEval,  ///< its FSR or DLSS call
    HleBatch,      ///< copy shader HLE: reading the ranges, batching and grouping them
    HleObtain,     ///< copy shader HLE: ObtainBuffer for the batches' sources and destinations
    HleCopy,       ///< copy shader HLE: recording the copies (MultiCopy or CopyBuffer)
    VramTrack,     ///< SynchronizeMemory: the memory tracker's walk over the range (with uploads)
    VramUpload,    ///< SynchronizeMemory: staging and recording the uploads it found
    Protect,       ///< page protection changes made on this thread (VirtualProtect)
    WaitCopies,    ///< Scheduler::WaitHostCopies: host copies not done yet
    WaitSignals,   ///< Scheduler::WaitDeferredSignals: fences deferred to the recording threads
    Count,
};
inline constexpr const char* Names[Count] = {
    "packet",       "tasks",       "draw",          "render state", "bind",
    "buffers",      "textures",    "begin render",  "vertex",       "index",
    "emit vertex",  "dynamic",     "dispatch",      "HLE shader",   "ObtainBuffer",
    "flush",        "motion",      "descriptors",   "kick",         "preupload",
    "EnsureResident", "stream copy", "VRAM path", "tex set", "tex set check", "tex set apply",
    "tex full", "samplers", "VRAM image sync", "VRAM written", "tex full empty",
    "refresh hash", "refresh obtain", "refresh detile", "refresh upload", "upscaler",
    "upscaler eval", "HLE batch", "HLE obtain", "HLE copy", "VRAM track", "VRAM upload",
    "protect", "wait copies", "wait fences",
};
inline std::array<std::atomic<std::uint64_t>, Count> cycles{};

/// BB_SECTIONS, read once at startup (a plain global: no initialization guard per scope).
inline const bool enabled = [] {
    const char* env = std::getenv("BB_SECTIONS");
    return env && env[0] == '1';
}();
inline bool Enabled() {
    return enabled;
}
/// Set on the draw recording thread (DrawPipe::Run): only its time is counted.
inline thread_local bool recording_thread = false;

/// Off (the default) a scope costs one predictable branch on `enabled` and one on `start` at
/// its end: no thread-local access, no rdtsc, no atomics.
struct Scope {
    explicit Scope(Id id_) : id{id_} {
        if (enabled) [[unlikely]] {
            start = recording_thread ? __rdtsc() : 0;
        }
    }
    ~Scope() {
        End();
    }
    /// Ends the scope before its block does (counted once).
    void End() {
        if (start) [[unlikely]] {
            cycles[id].fetch_add(__rdtsc() - start, std::memory_order_relaxed);
            start = 0;
        }
    }
    Id id;
    std::uint64_t start = 0;
};
/// Where a section's time goes, per key (a shader's program hash): open addressing over 1024
/// slots, written by the draw recording thread only; read and cleared with the statistics
/// (racy, diagnostics only).
struct ByKey {
    struct Entry {
        std::uint64_t key = 0, cycles = 0, calls = 0;
    };
    std::array<Entry, 1024> entries{};
    void Add(std::uint64_t key, std::uint64_t spent) {
        key |= 1; // 0 marks a free slot
        for (std::size_t i = 0; i < entries.size(); ++i) {
            auto& e = entries[(key + i) % entries.size()];
            if (e.key == key || e.key == 0) {
                e.key = key;
                e.cycles += spent;
                ++e.calls;
                return;
            }
        }
    }
    /// The `n` keys with the most time, most first; the table starts over.
    template <typename Print>
    void Top(std::size_t n, Print&& print) {
        std::array<Entry, 1024> copy = entries;
        entries = {};
        std::sort(copy.begin(), copy.end(),
                  [](const Entry& a, const Entry& b) { return a.cycles > b.cycles; });
        for (std::size_t i = 0; i < n && copy[i].key; ++i) {
            print(copy[i]);
        }
    }
};
inline ByKey tex_full_by_shader, dispatch_by_shader;

/// Times its block into `table` under `key` (BB_SECTIONS=1, draw recording thread).
struct KeyScope {
    KeyScope(ByKey& table_, std::uint64_t key_) : table{table_}, key{key_} {
        if (enabled) [[unlikely]] {
            start = recording_thread ? __rdtsc() : 0;
        }
    }
    ~KeyScope() {
        if (start) [[unlikely]] {
            table.Add(key, __rdtsc() - start);
        }
    }
    ByKey& table;
    std::uint64_t key;
    std::uint64_t start = 0;
};
} // namespace BbSections

#define BB_SECTION_JOIN2(a, b) a##b
#define BB_SECTION_JOIN(a, b) BB_SECTION_JOIN2(a, b)
#define BB_SECTION(id) BbSections::Scope BB_SECTION_JOIN(bb_section_scope_, __LINE__){BbSections::id}
