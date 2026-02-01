/**
 * RK3588 MPP JPEG Encoder - 智能模式：零拷贝 or memcpy
 * 
 * 当输入 buffer 满足 MPP 16 像素对齐要求时，使用 DMA-BUF fd 零拷贝
 * 否则使用 MPP 内部 buffer + memcpy
 */

#include "rk_internal.h"
#include <mpp_frame.h>
#include <mpp_packet.h>
#include <mpp_buffer.h>
#include <cstring>
#include <cstdlib>

#undef LOG_TAG
#define LOG_TAG "RK_MPP"

RkScreenshotError rk_mpp_init(RkMppEncoder* enc) {
    if (!enc) return RKSS_ERROR_INVALID_PARAM;

    memset(enc, 0, sizeof(*enc));

    MPP_RET ret = mpp_create(&enc->ctx, &enc->api);
    if (ret != MPP_OK) {
        ALOGE("❌ mpp_create failed: %d", ret);
        return RKSS_ERROR_ENCODE_FAILED;
    }

    ret = mpp_init(enc->ctx, MPP_CTX_ENC, MPP_VIDEO_CodingMJPEG);
    if (ret != MPP_OK) {
        ALOGE("❌ mpp_init failed: %d", ret);
        mpp_destroy(enc->ctx);
        return RKSS_ERROR_ENCODE_FAILED;
    }

    ret = mpp_enc_cfg_init(&enc->cfg);
    if (ret != MPP_OK) {
        ALOGE("❌ mpp_enc_cfg_init failed: %d", ret);
        mpp_destroy(enc->ctx);
        return RKSS_ERROR_ENCODE_FAILED;
    }

    enc->initialized = true;
    ALOGI("✅ MPP JPEG encoder ready");
    return RKSS_SUCCESS;
}

void rk_mpp_deinit(RkMppEncoder* enc) {
    if (!enc || !enc->initialized) return;

    if (enc->cfg) {
        mpp_enc_cfg_deinit(enc->cfg);
        enc->cfg = nullptr;
    }
    
    if (enc->ctx) {
        mpp_destroy(enc->ctx);
        enc->ctx = nullptr;
        enc->api = nullptr;
    }

    enc->initialized = false;
    ALOGI("MPP encoder stopped");
}

RkScreenshotError rk_mpp_encode_jpeg(
    RkMppEncoder* enc,
    RkDmaBuffer* src,
    uint8_t** out_data,
    size_t* out_size,
    int quality)
{
    if (!enc || !enc->initialized) return RKSS_ERROR_NOT_INITIALIZED;
    if (!src || !out_data || !out_size) return RKSS_ERROR_INVALID_PARAM;

    uint64_t t0 = rk_get_time_us();
    MPP_RET ret = MPP_OK;
    RkScreenshotError err = RKSS_SUCCESS;

    int width = src->width;
    int height = src->height;
    
    // MPP 需要 16 像素对齐
    int hor_stride_aligned = ((width + 15) / 16) * 16;
    int ver_stride_aligned = ((height + 15) / 16) * 16;
    int hor_stride_bytes = hor_stride_aligned * 4;
    
    // 零拷贝条件：width 和 height 都必须是 16 对齐
    bool zero_copy = (width == hor_stride_aligned) && 
                     (height == ver_stride_aligned) && 
                     (src->fd >= 0);
    
    // MPP JPEG quality: 0-10 (10=最高质量)
    int mpp_quant = (quality * 10 + 50) / 100;
    if (mpp_quant < 1) mpp_quant = 1;
    if (mpp_quant > 10) mpp_quant = 10;

    ALOGD("JPEG encode: %dx%d (aligned %dx%d), Q%d->%d, %s", 
          width, height, hor_stride_aligned, ver_stride_aligned, quality, mpp_quant,
          zero_copy ? "🚀 ZERO-COPY" : "📋 MEMCPY");

    // 配置编码参数
    mpp_enc_cfg_set_s32(enc->cfg, "prep:width", width);
    mpp_enc_cfg_set_s32(enc->cfg, "prep:height", height);
    mpp_enc_cfg_set_s32(enc->cfg, "prep:hor_stride", hor_stride_bytes);
    mpp_enc_cfg_set_s32(enc->cfg, "prep:ver_stride", ver_stride_aligned);
    mpp_enc_cfg_set_s32(enc->cfg, "prep:format", MPP_FMT_RGBA8888);
    mpp_enc_cfg_set_s32(enc->cfg, "jpeg:quant", mpp_quant);

    ret = enc->api->control(enc->ctx, MPP_ENC_SET_CFG, enc->cfg);
    if (ret != MPP_OK) {
        ALOGE("❌ MPP config failed: %d", ret);
        return RKSS_ERROR_ENCODE_FAILED;
    }

    MppFrame frame = nullptr;
    MppPacket packet = nullptr;
    MppBuffer frame_buf = nullptr;
    size_t frame_size = (size_t)hor_stride_bytes * ver_stride_aligned;
    
    // 分配输出缓冲
    void* pkt_data = malloc(frame_size);
    if (!pkt_data) {
        return RKSS_ERROR_NO_MEMORY;
    }

    if (zero_copy) {
        // ========== 零拷贝模式：直接使用 DMA-BUF fd ==========
        MppBufferInfo info;
        memset(&info, 0, sizeof(info));
        info.type = MPP_BUFFER_TYPE_DRM;
        info.fd = src->fd;
        info.size = src->size;
        info.ptr = nullptr;
        
        ret = mpp_buffer_import(&frame_buf, &info);
        if (ret != MPP_OK || !frame_buf) {
            ALOGW("⚠️ DMA-BUF import failed, fallback to memcpy");
            zero_copy = false;
        }
    }
    
    if (!zero_copy) {
        // ========== Memcpy 模式：使用 MPP 内部 buffer ==========
        // 从 MPP 内部 pool 分配 buffer
        ret = mpp_buffer_get(nullptr, &frame_buf, frame_size);
        if (ret != MPP_OK || !frame_buf) {
            ALOGE("❌ mpp_buffer_get failed: %d", ret);
            free(pkt_data);
            return RKSS_ERROR_NO_MEMORY;
        }
        
        // 映射源 DMA-BUF
        void* src_vir = rk_dmabuf_map(src);
        if (!src_vir) {
            ALOGE("❌ Failed to map source buffer");
            mpp_buffer_put(frame_buf);
            free(pkt_data);
            return RKSS_ERROR_ENCODE_FAILED;
        }
        
        // 获取 MPP buffer 的虚拟地址并拷贝数据
        void* frame_ptr = mpp_buffer_get_ptr(frame_buf);
        int src_stride = width * 4;
        
        if (hor_stride_bytes == src_stride) {
            memcpy(frame_ptr, src_vir, height * src_stride);
        } else {
            // 处理 stride 对齐
            uint8_t* dst_row = (uint8_t*)frame_ptr;
            uint8_t* src_row = (uint8_t*)src_vir;
            for (int y = 0; y < height; y++) {
                memcpy(dst_row, src_row, src_stride);
                dst_row += hor_stride_bytes;
                src_row += src_stride;
            }
        }
        rk_dmabuf_unmap(src);
    }
    
    // 创建 frame
    mpp_frame_init(&frame);
    mpp_frame_set_width(frame, width);
    mpp_frame_set_height(frame, height);
    mpp_frame_set_hor_stride(frame, hor_stride_aligned);
    mpp_frame_set_ver_stride(frame, ver_stride_aligned);
    mpp_frame_set_fmt(frame, MPP_FMT_RGBA8888);
    mpp_frame_set_eos(frame, 1);
    mpp_frame_set_buffer(frame, frame_buf);

    // 创建输出 packet
    mpp_packet_init(&packet, pkt_data, frame_size);
    mpp_packet_set_length(packet, 0);

    // 编码
    ret = enc->api->encode_put_frame(enc->ctx, frame);
    if (ret != MPP_OK) {
        ALOGE("❌ encode_put_frame failed: %d", ret);
        err = RKSS_ERROR_ENCODE_FAILED;
        goto cleanup;
    }

    ret = enc->api->encode_get_packet(enc->ctx, &packet);
    if (ret != MPP_OK || !packet) {
        ALOGE("❌ encode_get_packet failed: %d", ret);
        err = RKSS_ERROR_ENCODE_FAILED;
        goto cleanup;
    }

    // 获取输出数据
    {
        void* pkt_ptr = mpp_packet_get_pos(packet);
        size_t pkt_len = mpp_packet_get_length(packet);

        *out_data = (uint8_t*)malloc(pkt_len);
        if (!*out_data) {
            err = RKSS_ERROR_NO_MEMORY;
            goto cleanup;
        }
        
        memcpy(*out_data, pkt_ptr, pkt_len);
        *out_size = pkt_len;

        uint64_t elapsed = rk_get_time_us() - t0;
        ALOGI("✅ JPEG: %zu bytes in %.2f ms", pkt_len, elapsed / 1000.0);
    }

cleanup:
    // 重置编码器释放内部引用
    if (enc->api && enc->ctx) {
        enc->api->reset(enc->ctx);
    }
    
    if (frame) {
        mpp_frame_set_buffer(frame, nullptr);
        mpp_frame_deinit(&frame);
    }
    if (frame_buf) {
        mpp_buffer_put(frame_buf);
    }
    if (packet) {
        mpp_packet_deinit(&packet);
    }
    
    free(pkt_data);

    return err;
}
