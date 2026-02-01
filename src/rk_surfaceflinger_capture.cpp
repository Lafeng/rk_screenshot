/**
 * RK SurfaceFlinger Screenshot Capture
 * 
 * 使用 AOSP ScreenshotClient API，输出 DMA-BUF
 */

#include "rk_internal.h"

// AOSP APIs
#include <gui/SurfaceComposerClient.h>
#include <gui/ISurfaceComposer.h>
#include <gui/DisplayCaptureArgs.h>
#include <gui/SyncScreenCaptureListener.h>
#include <ui/GraphicBuffer.h>
#include <ui/DisplayState.h>
#include <binder/ProcessState.h>

#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <errno.h>

using namespace android;

#undef LOG_TAG
#define LOG_TAG "RK_SF"

static RkSurfaceFlingerContext g_sf_ctx = {};

RkScreenshotError rk_sf_init(RkSurfaceFlingerContext** out_ctx) {
    if (!out_ctx) return RKSS_ERROR_INVALID_PARAM;
    
    if (g_sf_ctx.initialized) {
        *out_ctx = &g_sf_ctx;
        return RKSS_SUCCESS;
    }

    // 启动 Binder 线程池（异步回调必需）
    // 设置至少 1 个线程避免警告
    ProcessState::self()->setThreadPoolMaxThreadCount(1);
    ProcessState::self()->startThreadPool();

    g_sf_ctx.initialized = true;
    g_sf_ctx.total_captures = 0;
    g_sf_ctx.total_time_us = 0;
    *out_ctx = &g_sf_ctx;

    ALOGI("✅ SurfaceFlinger capture ready");
    return RKSS_SUCCESS;
}

void rk_sf_deinit(RkSurfaceFlingerContext* ctx) {
    if (!ctx || !ctx->initialized) return;
    
    if (ctx->total_captures > 0) {
        ALOGI("SF stats: %lu captures, avg %.2f ms",
              ctx->total_captures, ctx->total_time_us / ctx->total_captures / 1000.0);
    }
    ctx->initialized = false;
}

RkScreenshotError rk_sf_capture(RkSurfaceFlingerContext* ctx, RkDmaBuffer** out_buf) {
    if (!ctx || !ctx->initialized) return RKSS_ERROR_NOT_INITIALIZED;
    if (!out_buf) return RKSS_ERROR_INVALID_PARAM;

    uint64_t t0 = rk_get_time_us();

    // 获取显示器
    sp<IBinder> display = SurfaceComposerClient::getInternalDisplayToken();
    if (!display) {
        ALOGE("❌ No display");
        return RKSS_ERROR_CAPTURE_FAILED;
    }

    // 获取显示器尺寸（用于调试日志）
    ui::DisplayState state;
    if (SurfaceComposerClient::getDisplayState(display, &state) != NO_ERROR) {
        ALOGE("❌ Failed to get display state");
        return RKSS_ERROR_CAPTURE_FAILED;
    }

    // 截图
    gui::DisplayCaptureArgs args;
    args.displayToken = display;
    args.width = 0;
    args.height = 0;
    args.useIdentityTransform = false;

    sp<SyncScreenCaptureListener> listener = sp<SyncScreenCaptureListener>::make();
    
    status_t err = ScreenshotClient::captureDisplay(args, listener);
    if (err != NO_ERROR) {
        ALOGE("❌ captureDisplay failed: %d", err);
        return RKSS_ERROR_CAPTURE_FAILED;
    }

    ScreenCaptureResults results = listener->waitForResults();
    if (results.result != NO_ERROR || !results.buffer) {
        ALOGE("❌ Capture failed: %d", results.result);
        return RKSS_ERROR_CAPTURE_FAILED;
    }

    sp<GraphicBuffer> buffer = results.buffer;

    // 导出 DMA-BUF fd
    const native_handle_t* handle = buffer->getNativeBuffer()->handle;
    if (!handle || handle->numFds < 1) {
        ALOGE("❌ No DMA-BUF fd in GraphicBuffer");
        return RKSS_ERROR_CAPTURE_FAILED;
    }

    int fd = dup(handle->data[0]);
    if (fd < 0) {
        ALOGE("❌ dup failed: %s", strerror(errno));
        return RKSS_ERROR_CAPTURE_FAILED;
    }

    // 创建输出结构
    RkDmaBuffer* buf = (RkDmaBuffer*)calloc(1, sizeof(RkDmaBuffer));
    if (!buf) {
        close(fd);
        return RKSS_ERROR_NO_MEMORY;
    }

    buf->fd = fd;
    buf->width = buffer->getWidth();
    buf->height = buffer->getHeight();
    buf->stride = buffer->getStride();
    buf->format = buffer->getPixelFormat();
    buf->size = buf->stride * buf->height * 4;

    uint64_t elapsed = rk_get_time_us() - t0;
    ctx->total_captures++;
    ctx->total_time_us += elapsed;

    ALOGD("📸 Captured %dx%d in %.2f ms (fd=%d)", buf->width, buf->height, elapsed / 1000.0, fd);

    *out_buf = buf;
    return RKSS_SUCCESS;
}
