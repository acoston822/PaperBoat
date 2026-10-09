// Temporary diagnostics: labels frame-pacing stalls with where they happened.
// Logged lines are tagged [frame-stats] so they sort with the once-a-second summary.
#include "FrameStats.h"

#include <spdlog/spdlog.h>

extern "C" {
void* get_current_map_config(void); // MapConfig*; its first member is the char* id
int get_game_mode(void);

static const char* CurMapId(void) {
    void* cfg = get_current_map_config();
    const char* id = cfg ? *(const char**) cfg : nullptr;
    return id ? id : "?";
}

uint64_t FrameEvents_Now(void) {
    return FrameStats::NowNs();
}

// One line per map load, with how long each phase took (all in ms).
void FrameEvents_MapLoad(const char* mapId, int area, int map, int loadType, uint64_t t0, uint64_t t1, uint64_t t2,
                         uint64_t t3, uint64_t t4, uint64_t t5) {
    auto ms = [](uint64_t a, uint64_t b) { return (b - a) / 1.0e6; };
    SPDLOG_INFO("[frame-stats] map load {} (area {} map {} type {}) total={:.1f}ms | setup+dma={:.1f} init={:.1f} "
                "shape={:.1f} reset={:.1f} models={:.1f}",
                mapId ? mapId : "?", area, map, loadType, ms(t0, t5), ms(t0, t1), ms(t1, t2), ms(t2, t3), ms(t3, t4),
                ms(t4, t5));
}

// One line per battle start: which battle and how long the load took.
void FrameEvents_BattleLoad(int battleId, uint64_t t0, uint64_t t1, uint64_t t2, uint64_t t3) {
    auto ms = [](uint64_t a, uint64_t b) { return (b - a) / 1.0e6; };
    SPDLOG_INFO("[frame-stats] battle load id={:#x} map={} total={:.1f}ms | sprites={:.1f} init={:.1f} section={:.1f}",
                battleId, CurMapId(), ms(t0, t3), ms(t0, t1), ms(t1, t2), ms(t2, t3));
}

void FrameEvents_Event(const char* what) {
    SPDLOG_INFO("[frame-stats] event {} map={} mode={}", what, CurMapId(), get_game_mode());
}

// Wraps step_game_loop: any single game step over 40 ms is logged with the map and game mode.
static uint64_t sStepStart = 0;
void FrameEvents_StepBegin(void) {
    sStepStart = FrameStats::NowNs();
}
void FrameEvents_StepEnd(void) {
    const double ms = (FrameStats::NowNs() - sStepStart) / 1.0e6;
    if (ms >= 40.0) {
        SPDLOG_INFO("[frame-stats] slow game step took {:.1f}ms map={} mode={}", ms, CurMapId(), get_game_mode());
    }
}
}
