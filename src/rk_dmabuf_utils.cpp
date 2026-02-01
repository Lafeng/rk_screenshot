/**
 * RK3588 DMA-BUF Utils
 * 
 * 使用 ION/DMA-HEAP 分配真正的 DMA-BUF
 */

#include "rk_internal.h"
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <cstdlib>

// Linux DMA-HEAP
#include <linux/dma-heap.h>

#undef LOG_TAG
#define LOG_TAG "RK_DMABUF"

// DMA-HEAP 设备路径
#define DMA_HEAP_PATH "/dev/dma_heap/system"
#define DMA_HEAP_CMA_PATH "/dev/dma_heap/cma"

static int g_heap_fd = -1;

static int open_dma_heap() {
    if (g_heap_fd >= 0) return g_heap_fd;
    
    // 优先使用 CMA heap（连续内存，RGA/MPP 需要）
    g_heap_fd = open(DMA_HEAP_CMA_PATH, O_RDWR);
    if (g_heap_fd >= 0) {
        ALOGD("Using DMA-HEAP: %s", DMA_HEAP_CMA_PATH);
        return g_heap_fd;
    }
    
    // fallback 到 system heap
    g_heap_fd = open(DMA_HEAP_PATH, O_RDWR);
    if (g_heap_fd >= 0) {
        ALOGD("Using DMA-HEAP: %s", DMA_HEAP_PATH);
        return g_heap_fd;
    }
    
    ALOGE("❌ Failed to open DMA-HEAP: %s", strerror(errno));
    return -1;
}

RkDmaBuffer* rk_dmabuf_alloc(int width, int height) {
    int heap_fd = open_dma_heap();
    if (heap_fd < 0) return nullptr;

    size_t size = (size_t)width * height * 4;  // RGBA8888
    
    struct dma_heap_allocation_data alloc = {};
    alloc.len = size;
    alloc.fd_flags = O_RDWR | O_CLOEXEC;

    if (ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &alloc) < 0) {
        ALOGE("❌ DMA-HEAP alloc failed: %s (size=%zu)", strerror(errno), size);
        return nullptr;
    }

    RkDmaBuffer* buf = (RkDmaBuffer*)calloc(1, sizeof(RkDmaBuffer));
    if (!buf) {
        close(alloc.fd);
        return nullptr;
    }

    buf->fd = alloc.fd;
    buf->size = size;
    buf->width = width;
    buf->height = height;
    buf->stride = width;
    buf->format = RK_FORMAT_RGBA8888;
    buf->vir_addr = nullptr;

    ALOGD("✅ Allocated DMA-BUF: fd=%d, %dx%d, %zu bytes", buf->fd, width, height, size);
    return buf;
}

void* rk_dmabuf_map(RkDmaBuffer* buf) {
    if (!buf) return nullptr;
    if (buf->vir_addr) return buf->vir_addr;  // 已映射
    if (buf->fd < 0) return nullptr;

    void* addr = mmap(nullptr, buf->size, PROT_READ | PROT_WRITE, MAP_SHARED, buf->fd, 0);
    if (addr == MAP_FAILED) {
        ALOGE("❌ mmap failed: fd=%d, %s", buf->fd, strerror(errno));
        return nullptr;
    }

    buf->vir_addr = addr;
    ALOGD("Mapped: fd=%d -> %p", buf->fd, addr);
    return addr;
}

void rk_dmabuf_unmap(RkDmaBuffer* buf) {
    if (!buf || !buf->vir_addr) return;
    
    munmap(buf->vir_addr, buf->size);
    buf->vir_addr = nullptr;
    ALOGD("Unmapped: fd=%d", buf->fd);
}

void rk_dmabuf_free(RkDmaBuffer* buf) {
    if (!buf) return;

    if (buf->vir_addr) {
        rk_dmabuf_unmap(buf);
    }
    
    if (buf->fd >= 0) {
        close(buf->fd);
    }
    
    free(buf);
    ALOGD("Freed DMA-BUF");
}

// 时间工具
uint64_t rk_get_time_us() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000ULL;
}
