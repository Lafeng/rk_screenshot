#ifndef RK_INTERNAL_H
#define RK_INTERNAL_H

/**
 * RK3588 Screenshot Engine - Internal Header
 * 
 * 专用于 RK3588 + SurfaceFlinger + RGA + MPP 零拷贝管线
 * 无 fallback，无虚拟地址模式，全 DMA-BUF
 */

#include "rk_screenshot.h"

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

// Android logging
#include <android/log.h>

#ifndef LOG_TAG
#define LOG_TAG "RK_Screenshot"
#endif

#ifndef ALOGE
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#endif

// C++ headers for SurfaceFlinger
#ifdef __cplusplus
#include <utils/RefBase.h>
#include <ui/DisplayId.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ============================================
// DMA-BUF 缓冲区（核心数据结构）
// ============================================
typedef struct {
    int fd;              // DMA-BUF 文件描述符
    size_t size;         // 缓冲区大小（字节）
    int width;           // 图像宽度（像素）
    int height;          // 图像高度（像素）
    int stride;          // 行步进（像素）
    int format;          // 像素格式
    void* vir_addr;      // mmap 后的虚拟地址
} RkDmaBuffer;

// DMA-BUF 操作
RkDmaBuffer* rk_dmabuf_alloc(int width, int height);
void* rk_dmabuf_map(RkDmaBuffer* buf);
void rk_dmabuf_unmap(RkDmaBuffer* buf);
void rk_dmabuf_free(RkDmaBuffer* buf);

// 时间工具
uint64_t rk_get_time_us(void);

#ifdef __cplusplus
}  // extern "C"

// ============================================
// SurfaceFlinger 上下文 (C++ only)
// ============================================
struct RkSurfaceFlingerContext {
    bool initialized;
    uint64_t total_captures;
    uint64_t total_time_us;
};

extern "C" {
#else
struct RkSurfaceFlingerContext;
#endif

// SurfaceFlinger 捕获
RkScreenshotError rk_sf_init(struct RkSurfaceFlingerContext** ctx);
void rk_sf_deinit(struct RkSurfaceFlingerContext* ctx);
RkScreenshotError rk_sf_capture(struct RkSurfaceFlingerContext* ctx, RkDmaBuffer** out);

#ifdef __cplusplus
}
#endif

// ============================================
// RGA 处理器
// ============================================
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool initialized;
    pthread_mutex_t lock;
    uint64_t total_ops;
    uint64_t total_time_us;
} RkRgaProcessor;

RkScreenshotError rk_rga_init(RkRgaProcessor* proc);
void rk_rga_deinit(RkRgaProcessor* proc);
RkScreenshotError rk_rga_process(RkRgaProcessor* proc, RkDmaBuffer* src, RkDmaBuffer* dst, int rotation);

#ifdef __cplusplus
}
#endif

// ============================================
// MPP JPEG 编码器
// ============================================
#include <rk_mpi.h>
#include <mpp_buffer.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    MppCtx ctx;
    MppApi* api;
    MppEncCfg cfg;
    MppBufferGroup buf_grp;
    bool initialized;
} RkMppEncoder;

RkScreenshotError rk_mpp_init(RkMppEncoder* enc);
void rk_mpp_deinit(RkMppEncoder* enc);
RkScreenshotError rk_mpp_encode_jpeg(RkMppEncoder* enc, RkDmaBuffer* src, 
                                     uint8_t** out_data, size_t* out_size, int quality);

#ifdef __cplusplus
}
#endif

// ============================================
// 全局上下文
// ============================================
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool initialized;
    struct RkSurfaceFlingerContext* sf_ctx;
    RkRgaProcessor rga;
    RkMppEncoder mpp;
} RkScreenshotContext;

#ifdef __cplusplus
}
#endif

#endif // RK_INTERNAL_H

