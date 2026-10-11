#include "OS.h"

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>

#include <SDL2/SDL_thread.h>
#include <chrono>
#include <dlfcn.h>
#include <pthread.h>
#include <spdlog/spdlog.h>

extern "C" {
#include "libultraship/libultra/message.h"
#include "libultraship/libultra/internal.h"
__OSEventState __osEventStateTab[OS_NUM_EVENTS] = { 0 };
}

namespace {

struct QueueSync {
    std::condition_variable notEmpty;
    std::condition_variable notFull;
    bool blockingEnabled = false;
};

std::mutex sMesgMutex;
std::map<OSMesgQueue*, QueueSync> sQueueSync;

QueueSync& SyncFor(OSMesgQueue* mq) {
    return sQueueSync[mq];
}

constexpr int kMaxBlockedWaits = 16;
struct BlockedWaitSlot {
    std::atomic<unsigned long> tid { 0 };
    std::atomic<OSMesgQueue*> mq { nullptr };
    std::atomic<int> isSend { 0 };
};
BlockedWaitSlot sBlockedWaits[kMaxBlockedWaits];

int MarkBlockedWait(OSMesgQueue* mq, int isSend) {
    unsigned long tid = (unsigned long) SDL_ThreadID();
    for (int i = 0; i < kMaxBlockedWaits; i++) {
        unsigned long expected = 0;
        if (sBlockedWaits[i].tid.compare_exchange_strong(expected, tid, std::memory_order_acq_rel)) {
            sBlockedWaits[i].mq.store(mq, std::memory_order_release);
            sBlockedWaits[i].isSend.store(isSend, std::memory_order_release);
            return i;
        }
    }
    return -1;
}

void ClearBlockedWait(int slot) {
    if (slot >= 0) {
        sBlockedWaits[slot].mq.store(nullptr, std::memory_order_release);
        sBlockedWaits[slot].tid.store(0, std::memory_order_release);
    }
}

// Diagnostics: report blocking queue waits that take ≥ 60 ms (frame-pacing stall hunt).
void ReportSlowWait(OSMesgQueue* mq, bool isSend, double ms, void* caller) {
    char tname[32] = "?";
    pthread_getname_np(pthread_self(), tname, sizeof(tname));
    Dl_info info {};
    const char* sym = "?";
    long off = 0;
    if (dladdr(caller, &info)) {
        if (info.dli_sname) {
            sym = info.dli_sname;
            off = (char*) caller - (char*) info.dli_saddr;
        } else if (info.dli_fbase) {
            off = (char*) caller - (char*) info.dli_fbase;
        }
    }
    SPDLOG_INFO("[frame-stats] slow queue {} wait {:.1f}ms thread={} tid={} queue={} caller={}+{:#x}",
                isSend ? "send" : "recv", ms, tname, (unsigned long) SDL_ThreadID(), (void*) mq, sym, off);
}

bool WaitOn(std::unique_lock<std::mutex>& lock, OSMesgQueue* mq, QueueSync& sync, int32_t flag, bool isSend,
            void* caller = nullptr) {
    int waitSlot = -1;
    std::chrono::steady_clock::time_point waitStart;
    bool waited = false;
    while (isSend ? mq->validCount >= mq->msgCount : mq->validCount == 0) {
        if (flag != OS_MESG_BLOCK || !sync.blockingEnabled) {
            ClearBlockedWait(waitSlot);
            return false;
        }
        if (waitSlot < 0) {
            waitSlot = MarkBlockedWait(mq, isSend ? 1 : 0);
            waitStart = std::chrono::steady_clock::now();
            waited = true;
        }
        (isSend ? sync.notFull : sync.notEmpty).wait(lock);
    }
    ClearBlockedWait(waitSlot);
    if (waited) {
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - waitStart).count();
        if (ms >= 60.0) {
            ReportSlowWait(mq, isSend, ms, caller);
        }
    }
    return true;
}

} // namespace

extern "C" {

void osCreateMesgQueue(OSMesgQueue* mq, OSMesg* msgBuf, int32_t count) {
    std::lock_guard<std::mutex> lock(sMesgMutex);
    mq->mtqueue = nullptr;
    mq->fullqueue = nullptr;
    mq->validCount = 0;
    mq->first = 0;
    mq->msgCount = count;
    mq->msg = msgBuf;
    sQueueSync[mq];
}

int32_t osSendMesg(OSMesgQueue* mq, OSMesg msg, int32_t flag) {
    std::unique_lock<std::mutex> lock(sMesgMutex);
    QueueSync& sync = SyncFor(mq);
    if (!WaitOn(lock, mq, sync, flag, true, __builtin_return_address(0))) {
        return -1;
    }
    s32 last = (mq->first + mq->validCount) % mq->msgCount;
    mq->msg[last] = msg;
    mq->validCount++;
    sync.notEmpty.notify_one();
    return 0;
}

int32_t osJamMesg(OSMesgQueue* mq, OSMesg msg, int32_t flag) {
    std::unique_lock<std::mutex> lock(sMesgMutex);
    QueueSync& sync = SyncFor(mq);
    if (!WaitOn(lock, mq, sync, flag, true)) {
        return -1;
    }
    mq->first = (mq->first + mq->msgCount - 1) % mq->msgCount;
    mq->msg[mq->first] = msg;
    mq->validCount++;
    sync.notEmpty.notify_one();
    return 0;
}

int32_t osRecvMesg(OSMesgQueue* mq, OSMesg* msg, int32_t flag) {
    std::unique_lock<std::mutex> lock(sMesgMutex);
    QueueSync& sync = SyncFor(mq);
    if (!WaitOn(lock, mq, sync, flag, false, __builtin_return_address(0))) {
        return -1;
    }
    if (msg != nullptr) {
        *msg = mq->msg[mq->first];
    }
    mq->first = (mq->first + 1) % mq->msgCount;
    mq->validCount--;
    sync.notFull.notify_one();
    return 0;
}

void osSetEventMesg(OSEvent event, OSMesgQueue* mq, OSMesg msg) {
    std::lock_guard<std::mutex> lock(sMesgMutex);
    if (event < OS_NUM_EVENTS) {
        __osEventStateTab[event].queue = mq;
        __osEventStateTab[event].msg = msg;
    }
}

void OS_SetQueueBlocking(void* mq, int enabled) {
    std::lock_guard<std::mutex> lock(sMesgMutex);
    SyncFor((OSMesgQueue*) mq).blockingEnabled = (enabled != 0);
}

void OS_BeginShutdown(void) {
    std::lock_guard<std::mutex> lock(sMesgMutex);
    for (auto& [mq, sync] : sQueueSync) {
        (void) mq;
        sync.blockingEnabled = false;
        sync.notEmpty.notify_all();
        sync.notFull.notify_all();
    }
}

// Lock-free: the caller may be looking at a deadlock.
int OS_MesgSnapshotBlockedWaits(OS_BlockedWait* out, int max) {
    int n = 0;
    for (int i = 0; i < kMaxBlockedWaits && n < max; i++) {
        unsigned long tid = sBlockedWaits[i].tid.load(std::memory_order_acquire);
        OSMesgQueue* mq = sBlockedWaits[i].mq.load(std::memory_order_acquire);
        if (tid == 0 || mq == nullptr) {
            continue;
        }
        out[n].tid = tid;
        out[n].mq = mq;
        out[n].isSend = sBlockedWaits[i].isSend.load(std::memory_order_acquire);
        n++;
    }
    return n;
}

void OS_SendEventMesg(OSEvent event) {
    OSMesgQueue* mq = nullptr;
    OSMesg msg = {};
    {
        std::lock_guard<std::mutex> lock(sMesgMutex);
        if (event >= OS_NUM_EVENTS || __osEventStateTab[event].queue == nullptr) {
            return;
        }
        mq = __osEventStateTab[event].queue;
        msg = __osEventStateTab[event].msg;
    }
    osSendMesg(mq, msg, OS_MESG_NOBLOCK);
}

void OS_JamEventMesg(OSEvent event) {
    OSMesgQueue* mq = nullptr;
    OSMesg msg = {};
    {
        std::lock_guard<std::mutex> lock(sMesgMutex);
        if (event >= OS_NUM_EVENTS || __osEventStateTab[event].queue == nullptr) {
            return;
        }
        mq = __osEventStateTab[event].queue;
        msg = __osEventStateTab[event].msg;
    }
    osJamMesg(mq, msg, OS_MESG_NOBLOCK);
}

} // extern "C"
