#pragma once
// Temporary frame-time diagnostics. Once a second the main loop writes one line to the log with
// how long each stage of the frame took and how evenly frames were presented.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>

namespace FrameStats {

enum Slot { kStart, kSi, kDrain, kRender, kIdle, kPresent, kRetrace, kSlotCount };

struct Counter {
    std::atomic<uint64_t> n { 0 }, sumNs { 0 }, maxNs { 0 }, over25 { 0 }, over40 { 0 };
};

inline Counter gCounters[kSlotCount];

inline uint64_t NowNs() {
    return (uint64_t) std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

inline void Add(Slot s, uint64_t ns) {
    Counter& c = gCounters[s];
    c.n.fetch_add(1, std::memory_order_relaxed);
    c.sumNs.fetch_add(ns, std::memory_order_relaxed);
    uint64_t prev = c.maxNs.load(std::memory_order_relaxed);
    while (ns > prev && !c.maxNs.compare_exchange_weak(prev, ns, std::memory_order_relaxed)) {
    }
    if (ns > 25000000ull) {
        c.over25.fetch_add(1, std::memory_order_relaxed);
    }
    if (ns > 40000000ull) {
        c.over40.fetch_add(1, std::memory_order_relaxed);
    }
}

// Records the gap since the previous call from the same site.
inline void Tick(Slot s, std::atomic<uint64_t>& last) {
    const uint64_t now = NowNs();
    const uint64_t prev = last.exchange(now, std::memory_order_relaxed);
    if (prev != 0) {
        Add(s, now - prev);
    }
}

struct Snap {
    uint64_t n, avgUs, maxUs, o25, o40;
};

inline Snap Take(Slot s) {
    Counter& c = gCounters[s];
    Snap r {};
    r.n = c.n.exchange(0);
    const uint64_t sum = c.sumNs.exchange(0);
    r.maxUs = c.maxNs.exchange(0) / 1000;
    r.o25 = c.over25.exchange(0);
    r.o40 = c.over40.exchange(0);
    r.avgUs = r.n ? sum / r.n / 1000 : 0;
    return r;
}

// Call from the main loop. Returns true about once a second with a summary line in `out`.
inline bool Poll(char* out, size_t cap) {
    static uint64_t next = 0;
    const uint64_t now = NowNs();
    if (next == 0) {
        next = now + 1000000000ull;
        return false;
    }
    if (now < next) {
        return false;
    }
    next = now + 1000000000ull;
    const Snap st = Take(kStart), si = Take(kSi), dr = Take(kDrain), re = Take(kRender), id = Take(kIdle),
               pr = Take(kPresent), rt = Take(kRetrace);
    std::snprintf(
        out, cap,
        "[frame-stats] present n=%llu avg=%lluus max=%lluus >25ms=%llu >40ms=%llu | retrace n=%llu avg=%lluus "
        "max=%lluus >25ms=%llu | render n=%llu avg=%lluus max=%lluus | startFrame max=%lluus | si max=%lluus | "
        "drain max=%lluus | idleSleeps=%llu max=%lluus",
        (unsigned long long) pr.n, (unsigned long long) pr.avgUs, (unsigned long long) pr.maxUs,
        (unsigned long long) pr.o25, (unsigned long long) pr.o40, (unsigned long long) rt.n,
        (unsigned long long) rt.avgUs, (unsigned long long) rt.maxUs, (unsigned long long) rt.o25,
        (unsigned long long) re.n, (unsigned long long) re.avgUs, (unsigned long long) re.maxUs,
        (unsigned long long) st.maxUs, (unsigned long long) si.maxUs, (unsigned long long) dr.maxUs,
        (unsigned long long) id.n, (unsigned long long) id.maxUs
    );
    return true;
}

} // namespace FrameStats
