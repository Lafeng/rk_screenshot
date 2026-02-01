/**
 * RK3588 RGA Processor - DMA-BUF Only
 */

#include "rk_internal.h"
#include <im2d.h>
#include <RgaUtils.h>
#include <string.h>

#undef LOG_TAG
#define LOG_TAG "RK_RGA"

RkScreenshotError rk_rga_init(RkRgaProcessor* proc) {
    if (!proc) return RKSS_ERROR_INVALID_PARAM;

    memset(proc, 0, sizeof(*proc));
    
    const char* version = querystring(RGA_VERSION);
    if (!version) {
        ALOGE("❌ RGA not available");
        return RKSS_ERROR_RGA_FAILED;
    }

    ALOGI("RGA: %s", version);
    pthread_mutex_init(&proc->lock, NULL);
    proc->initialized = true;
    return RKSS_SUCCESS;
}

void rk_rga_deinit(RkRgaProcessor* proc) {
    if (!proc || !proc->initialized) return;
    
    if (proc->total_ops > 0) {
        ALOGI("RGA stats: %lu ops, avg %.2f ms",
              proc->total_ops, proc->total_time_us / proc->total_ops / 1000.0);
    }
    
    pthread_mutex_destroy(&proc->lock);
    proc->initialized = false;
}

RkScreenshotError rk_rga_process(
    RkRgaProcessor* proc,
    RkDmaBuffer* src,
    RkDmaBuffer* dst,
    int rotation)
{
    if (!proc || !proc->initialized) return RKSS_ERROR_NOT_INITIALIZED;
    if (!src || !dst || src->fd < 0 || dst->fd < 0) return RKSS_ERROR_INVALID_PARAM;

    pthread_mutex_lock(&proc->lock);
    uint64_t t0 = rk_get_time_us();

    // 使用 DMA-BUF fd 创建 RGA buffer
    rga_buffer_t rga_src = wrapbuffer_fd(src->fd, src->width, src->height,
                                         RK_FORMAT_RGBA_8888, src->stride, src->height);
    rga_buffer_t rga_dst = wrapbuffer_fd(dst->fd, dst->width, dst->height,
                                         RK_FORMAT_RGBA_8888, dst->stride, dst->height);

    IM_STATUS status;
    
    if (rotation == 90) {
        status = imrotate(rga_src, rga_dst, IM_HAL_TRANSFORM_ROT_90);
    } else if (rotation == 180) {
        status = imrotate(rga_src, rga_dst, IM_HAL_TRANSFORM_ROT_180);
    } else if (rotation == 270) {
        status = imrotate(rga_src, rga_dst, IM_HAL_TRANSFORM_ROT_270);
    } else if (src->width == dst->width && src->height == dst->height) {
        status = imcopy(rga_src, rga_dst);
    } else {
        status = imresize(rga_src, rga_dst);
    }

    uint64_t elapsed = rk_get_time_us() - t0;
    proc->total_ops++;
    proc->total_time_us += elapsed;
    
    pthread_mutex_unlock(&proc->lock);

    if (status != IM_STATUS_SUCCESS) {
        ALOGE("❌ RGA failed: %s", imStrError(status));
        return RKSS_ERROR_RGA_FAILED;
    }

    ALOGD("✅ RGA: %dx%d -> %dx%d in %.2f ms",
          src->width, src->height, dst->width, dst->height, elapsed / 1000.0);
    return RKSS_SUCCESS;
}
