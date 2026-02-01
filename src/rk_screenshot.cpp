/**
 * RK3588 Screenshot Engine - Main Module
 * 
 * SurfaceFlinger -> RGA -> MPP 零拷贝管线
 */

#include "rk_internal.h"
#include <cstring>
#include <cstdlib>

#undef LOG_TAG
#define LOG_TAG "RK_Screenshot"

static RkScreenshotContext g_ctx = {};

// ============================================
// 公共 API
// ============================================

const char* rk_screenshot_get_version() {
    return "2.0.0-dmabuf";
}

RkScreenshotError rk_screenshot_init() {
    if (g_ctx.initialized) {
        return RKSS_SUCCESS;
    }

    ALOGI("========================================");
    ALOGI("🚀 RK3588 Screenshot Engine v2.0");
    ALOGI("   Pipeline: SF -> RGA -> MPP (DMA-BUF)");
    ALOGI("========================================");

    // 1. SurfaceFlinger
    RkScreenshotError err = rk_sf_init(&g_ctx.sf_ctx);
    if (err != RKSS_SUCCESS) {
        ALOGE("❌ SurfaceFlinger init failed");
        return err;
    }
    ALOGI("✅ SurfaceFlinger ready");

    // 2. RGA
    err = rk_rga_init(&g_ctx.rga);
    if (err != RKSS_SUCCESS) {
        ALOGE("❌ RGA init failed");
        rk_sf_deinit(g_ctx.sf_ctx);
        return err;
    }
    ALOGI("✅ RGA ready");

    // 3. MPP
    err = rk_mpp_init(&g_ctx.mpp);
    if (err != RKSS_SUCCESS) {
        ALOGE("❌ MPP init failed");
        rk_rga_deinit(&g_ctx.rga);
        rk_sf_deinit(g_ctx.sf_ctx);
        return err;
    }
    ALOGI("✅ MPP ready");

    g_ctx.initialized = true;
    ALOGI("========================================");
    return RKSS_SUCCESS;
}

void rk_screenshot_deinit() {
    if (!g_ctx.initialized) return;

    rk_mpp_deinit(&g_ctx.mpp);
    rk_rga_deinit(&g_ctx.rga);
    rk_sf_deinit(g_ctx.sf_ctx);
    g_ctx.sf_ctx = nullptr;
    g_ctx.initialized = false;

    ALOGI("🔴 Screenshot engine stopped");
}

void rk_screenshot_get_default_config(RkScreenshotConfig* cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->width = 1920;
    cfg->height = 1080;
    cfg->format = RK_FORMAT_RGBA8888;
    cfg->quality = 90;
}

RkScreenshotError rk_screenshot_capture(
    const RkScreenshotConfig* cfg,
    RkScreenshotResult** result)
{
    if (!g_ctx.initialized) return RKSS_ERROR_NOT_INITIALIZED;
    if (!cfg || !result) return RKSS_ERROR_INVALID_PARAM;

    uint64_t t_start = rk_get_time_us();
    RkScreenshotError err;

    // 分配结果
    RkScreenshotResult* res = (RkScreenshotResult*)calloc(1, sizeof(RkScreenshotResult));
    if (!res) return RKSS_ERROR_NO_MEMORY;

    // ========== 阶段 1: 屏幕捕获 ==========
    uint64_t t_capture = rk_get_time_us();
    RkDmaBuffer* capture_buf = nullptr;
    
    err = rk_sf_capture(g_ctx.sf_ctx, &capture_buf);
    if (err != RKSS_SUCCESS) {
        free(res);
        return err;
    }
    
    res->capture_time_us = rk_get_time_us() - t_capture;
    ALOGD("📸 Capture: %.2f ms (%dx%d)", 
          res->capture_time_us / 1000.0, capture_buf->width, capture_buf->height);

    // ========== 阶段 2: RGA 缩放（可选）==========
    RkDmaBuffer* process_buf = capture_buf;
    bool need_scale = (cfg->scale_width > 0 && cfg->scale_height > 0) &&
                      (cfg->scale_width != capture_buf->width || 
                       cfg->scale_height != capture_buf->height);

    if (need_scale) {
        uint64_t t_rga = rk_get_time_us();
        
        RkDmaBuffer* scaled_buf = rk_dmabuf_alloc(cfg->scale_width, cfg->scale_height);
        if (!scaled_buf) {
            rk_dmabuf_free(capture_buf);
            free(res);
            return RKSS_ERROR_NO_MEMORY;
        }

        err = rk_rga_process(&g_ctx.rga, capture_buf, scaled_buf, cfg->rotation);
        if (err != RKSS_SUCCESS) {
            rk_dmabuf_free(scaled_buf);
            rk_dmabuf_free(capture_buf);
            free(res);
            return err;
        }

        res->process_time_us = rk_get_time_us() - t_rga;
        ALOGD("🔄 RGA: %.2f ms (%dx%d -> %dx%d)",
              res->process_time_us / 1000.0,
              capture_buf->width, capture_buf->height,
              scaled_buf->width, scaled_buf->height);

        rk_dmabuf_free(capture_buf);
        process_buf = scaled_buf;
    }

    // ========== 阶段 3: 输出 ==========
    if (cfg->format == RK_FORMAT_JPEG) {
        // JPEG 编码
        uint64_t t_enc = rk_get_time_us();
        
        err = rk_mpp_encode_jpeg(&g_ctx.mpp, process_buf, 
                                 &res->data, &res->size, cfg->quality);
        if (err != RKSS_SUCCESS) {
            rk_dmabuf_free(process_buf);
            free(res);
            return err;
        }

        res->encode_time_us = rk_get_time_us() - t_enc;
        ALOGD("🖼️  JPEG: %.2f ms (%zu bytes, Q%d)",
              res->encode_time_us / 1000.0, res->size, cfg->quality);
    } else {
        // 原始 RGBA
        void* vir = rk_dmabuf_map(process_buf);
        if (!vir) {
            rk_dmabuf_free(process_buf);
            free(res);
            return RKSS_ERROR_CAPTURE_FAILED;
        }

        res->size = process_buf->size;
        res->data = (uint8_t*)malloc(res->size);
        if (!res->data) {
            rk_dmabuf_free(process_buf);
            free(res);
            return RKSS_ERROR_NO_MEMORY;
        }
        memcpy(res->data, vir, res->size);
    }

    // 填充结果
    res->width = process_buf->width;
    res->height = process_buf->height;
    res->format = cfg->format;
    res->timestamp_us = t_start;

    rk_dmabuf_free(process_buf);

    // 总结
    uint64_t total = rk_get_time_us() - t_start;
    ALOGI("📊 Total: %.2f ms | Capture %.2f + RGA %.2f + Encode %.2f | %.1f FPS",
          total / 1000.0,
          res->capture_time_us / 1000.0,
          res->process_time_us / 1000.0,
          res->encode_time_us / 1000.0,
          1000000.0 / total);

    *result = res;
    return RKSS_SUCCESS;
}

void rk_screenshot_free_result(RkScreenshotResult* res) {
    if (!res) return;
    free(res->data);
    free(res);
}

const char* rk_screenshot_error_string(RkScreenshotError err) {
    switch (err) {
        case RKSS_SUCCESS: return "Success";
        case RKSS_ERROR_NOT_INITIALIZED: return "Not initialized";
        case RKSS_ERROR_INVALID_PARAM: return "Invalid parameter";
        case RKSS_ERROR_NO_MEMORY: return "Out of memory";
        case RKSS_ERROR_CAPTURE_FAILED: return "Capture failed";
        case RKSS_ERROR_RGA_FAILED: return "RGA failed";
        case RKSS_ERROR_ENCODE_FAILED: return "Encode failed";
        default: return "Unknown error";
    }
}
