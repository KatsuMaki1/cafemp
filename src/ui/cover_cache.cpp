#include "ui/cover_cache.hpp"

#include "logger/logger.hpp"
#include "net/http_client.hpp"

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <gx2/mem.h>
#include <gx2/sampler.h>
#include <gx2/texture.h>
#include <gx2/utils.h>
#include <gx2r/surface.h>
#include <mutex>
#include <stb_image.h>
#include <thread>
#include <unordered_map>

namespace ui_covers {

// Matches ImGui_ImplGX2_Texture layout (GX2Texture + GX2Sampler at offset 0).
struct Gx2Tex {
    GX2Texture Texture;
    GX2Sampler Sampler;
    int width = 0;
    int height = 0;
};

struct DecodedImage {
    std::string url;
    unsigned char *rgba = nullptr;
    int w = 0;
    int h = 0;
};

enum class SlotState { Pending, Ready, Failed };

struct Slot {
    SlotState state = SlotState::Pending;
    Gx2Tex *tex = nullptr;
};

static std::mutex g_mtx;
static std::condition_variable g_cv;
static std::unordered_map<std::string, Slot> g_slots;
static std::deque<std::string> g_queue;
static std::deque<DecodedImage> g_decoded;
static std::thread g_worker;
static std::atomic<bool> g_running{false};

static Gx2Tex *upload_gx2(const unsigned char *rgba, int w, int h) {
    if (!rgba || w <= 0 || h <= 0)
        return nullptr;
    Gx2Tex *t = new Gx2Tex();
    memset(&t->Texture, 0, sizeof(t->Texture));

    GX2Surface &surf = t->Texture.surface;
    surf.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    surf.use = GX2_SURFACE_USE_TEXTURE;
    surf.width = (uint32_t)w;
    surf.height = (uint32_t)h;
    surf.depth = 1;
    surf.mipLevels = 1;
    surf.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    surf.aa = GX2_AA_MODE1X;
    surf.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;

    t->Texture.viewNumSlices = 1;
    t->Texture.viewNumMips = 1;
    t->Texture.compMap = GX2_COMP_MAP(GX2_SQ_SEL_A, GX2_SQ_SEL_B, GX2_SQ_SEL_G, GX2_SQ_SEL_R);

    if (!GX2RCreateSurface(&surf, GX2R_RESOURCE_BIND_TEXTURE | GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ)) {
        delete t;
        return nullptr;
    }
    GX2InitTextureRegs(&t->Texture);

    uint32_t *dst = reinterpret_cast<uint32_t *>(GX2RLockSurfaceEx(&surf, 0, GX2R_RESOURCE_BIND_NONE));
    if (dst) {
        const uint32_t *src = reinterpret_cast<const uint32_t *>(rgba);
        for (int y = 0; y < h; y++)
            memcpy(&dst[y * surf.pitch], &src[y * w], (size_t)w * 4);
        GX2RUnlockSurfaceEx(&surf, 0, GX2R_RESOURCE_BIND_NONE);
    }

    GX2InitSampler(&t->Sampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_LINEAR);
    t->width = w;
    t->height = h;
    return t;
}

static void free_gx2(Gx2Tex *t) {
    if (!t)
        return;
    GX2RDestroySurfaceEx(&t->Texture.surface, GX2R_RESOURCE_BIND_NONE);
    delete t;
}

static void worker_loop() {
    http::Session session;
    while (g_running.load()) {
        std::string url;
        {
            std::unique_lock<std::mutex> lk(g_mtx);
            g_cv.wait_for(lk, std::chrono::milliseconds(100), [] { return !g_running.load() || !g_queue.empty(); });
            if (!g_running.load())
                break;
            if (g_queue.empty())
                continue;
            url = std::move(g_queue.front());
            g_queue.pop_front();
        }

        http::Response r = session.get({url, url, {}, "", true, 8});
        if (!r.ok() || r.body.empty()) {
            std::lock_guard<std::mutex> lk(g_mtx);
            g_slots[url].state = SlotState::Failed;
            continue;
        }

        int w = 0, h = 0, ch = 0;
        unsigned char *rgba = stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(r.body.data()), (int)r.body.size(), &w, &h, &ch, 4);
        if (!rgba || w <= 0 || h <= 0) {
            if (rgba)
                stbi_image_free(rgba);
            std::lock_guard<std::mutex> lk(g_mtx);
            g_slots[url].state = SlotState::Failed;
            continue;
        }

        std::lock_guard<std::mutex> lk(g_mtx);
        g_decoded.push_back({url, rgba, w, h});
    }
}

static void ensure_worker() {
    if (g_running.load())
        return;
    g_running.store(true);
    g_worker = std::thread(worker_loop);
}

static void pump_decoded() {
    std::deque<DecodedImage> ready;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        ready.swap(g_decoded);
    }
    for (DecodedImage &img : ready) {
        Gx2Tex *tex = upload_gx2(img.rgba, img.w, img.h);
        stbi_image_free(img.rgba);
        std::lock_guard<std::mutex> lk(g_mtx);
        auto &slot = g_slots[img.url];
        if (tex) {
            slot.tex = tex;
            slot.state = SlotState::Ready;
        } else {
            slot.state = SlotState::Failed;
        }
    }
}

ImTextureID get(const std::string &url) {
    if (url.empty())
        return 0;
    pump_decoded();

    std::lock_guard<std::mutex> lk(g_mtx);
    auto it = g_slots.find(url);
    if (it != g_slots.end()) {
        if (it->second.state == SlotState::Ready && it->second.tex)
            return reinterpret_cast<ImTextureID>(it->second.tex);
        return 0;
    }

    ensure_worker();
    g_slots[url] = Slot{SlotState::Pending, nullptr};
    g_queue.push_back(url);
    g_cv.notify_one();
    return 0;
}

void shutdown() {
    if (g_running.exchange(false)) {
        g_cv.notify_all();
        if (g_worker.joinable())
            g_worker.join();
    }
    std::lock_guard<std::mutex> lk(g_mtx);
    for (DecodedImage &img : g_decoded)
        stbi_image_free(img.rgba);
    g_decoded.clear();
    g_queue.clear();
    for (auto &kv : g_slots)
        free_gx2(kv.second.tex);
    g_slots.clear();
}

} // namespace ui_covers

