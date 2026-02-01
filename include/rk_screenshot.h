#ifndef RK_SCREENSHOT_H
#define RK_SCREENSHOT_H

// AOSP 兼容层（必须在 <memory> 之前）
// #include "aosp_compat.h"

#include <stdint.h>
#include <stddef.h>
#include <memory>
#include <functional>
#include <vector>

// ============================================
// 版本信息
// ============================================
#define RK_SCREENSHOT_VERSION_MAJOR 1
#define RK_SCREENSHOT_VERSION_MINOR 0
#define RK_SCREENSHOT_VERSION_PATCH 0

// ============================================
// 导出宏
// ============================================
#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) || defined(_WIN64)
    #ifdef RK3588_SCREENSHOT_EXPORTS
        #define RK_API __declspec(dllexport)
    #else
        #define RK_API __declspec(dllimport)
    #endif
#else
    #define RK_API __attribute__((visibility("default")))
#endif

// ============================================
// 错误码定义
// ============================================
typedef enum {
    RKSS_SUCCESS = 0,
    RKSS_ERROR_INVALID_PARAM = -1,
    RKSS_ERROR_NOT_INITIALIZED = -2,
    RKSS_ERROR_ALREADY_INITIALIZED = -3,
    RKSS_ERROR_INIT_FAILED = -4,
    RKSS_ERROR_NO_MEMORY = -5,
    RKSS_ERROR_DRM_FAILED = -6,
    RKSS_ERROR_RGA_FAILED = -7,
    RKSS_ERROR_MPP_FAILED = -8,
    RKSS_ERROR_NPU_FAILED = -9,
    RKSS_ERROR_CAPTURE_FAILED = -10,
    RKSS_ERROR_ENCODE_FAILED = -11,
    RKSS_ERROR_UNSUPPORTED = -12,
    RKSS_ERROR_TIMEOUT = -13,
    RKSS_ERROR_DEVICE_BUSY = -14,
} RkScreenshotError;

// ============================================
// 图像格式定义
// ============================================
typedef enum {
    // RGB 格式
    RK_FORMAT_RGBA8888 = 0,
    RK_FORMAT_RGBX8888 = 1,
    RK_FORMAT_RGB888 = 2,
    RK_FORMAT_BGR888 = 3,
    
    // YUV 格式
    RK_FORMAT_YUV420SP = 10,  // NV12
    RK_FORMAT_YUV420P = 11,   // I420
    
    // 压缩格式
    RK_FORMAT_JPEG = 20,
    RK_FORMAT_H264 = 21,
    RK_FORMAT_H265 = 22,
    RK_FORMAT_VP8 = 23,
    RK_FORMAT_VP9 = 24,
} RkImageFormat;

// ============================================
// 截图配置
// ============================================
typedef struct {
    // 分辨率
    int32_t width;
    int32_t height;
    
    // 输出格式
    RkImageFormat format;
    
    // 质量参数 (0-100, 仅用于压缩格式)
    int32_t quality;
    
    // 旋转角度 (0, 90, 180, 270)
    int32_t rotation;
    
    // 是否垂直翻转
    bool flip_vertical;
    
    // 是否水平翻转
    bool flip_horizontal;
    
    // 裁剪区域 (0 表示不裁剪)
    int32_t crop_x;
    int32_t crop_y;
    int32_t crop_width;
    int32_t crop_height;
    
    // 缩放目标尺寸 (0 表示不缩放)
    int32_t scale_width;
    int32_t scale_height;
    
    // 是否启用 NPU 增强
    bool enable_npu_enhance;
    
    // 超时时间 (毫秒)
    int32_t timeout_ms;
    
    // 保留字段
    uint32_t reserved[8];
} RkScreenshotConfig;

// ============================================
// 截图结果
// ============================================
typedef struct {
    // 图像数据
    uint8_t* data;
    size_t size;
    
    // 实际尺寸
    int32_t width;
    int32_t height;
    
    // 格式
    RkImageFormat format;
    
    // 时间戳 (微秒)
    int64_t timestamp_us;
    
    // 捕获耗时 (微秒)
    int64_t capture_time_us;
    
    // 处理耗时 (微秒)
    int64_t process_time_us;
    
    // 编码耗时 (微秒)
    int64_t encode_time_us;
    
    // 总耗时（微秒）
    int64_t total_time_us;      
    
    // 保留字段
    uint32_t reserved[8];
} RkScreenshotResult;

// ============================================
// 硬件能力信息
// ============================================
typedef struct {
    // DRM 信息
    bool drm_available;
    char drm_device[64];
    int32_t display_width;
    int32_t display_height;
    int32_t display_refresh_rate;
    
    // RGA 信息
    bool rga_available;
    char rga_version[64];
    int32_t rga_max_width;
    int32_t rga_max_height;
    
    // MPP 信息
    bool mpp_available;
    char mpp_version[64];
    bool support_jpeg;
    bool support_h264;
    bool support_h265;
    bool support_vp8;
    bool support_vp9;
    
    // NPU 信息
    bool npu_available;
    char npu_version[64];
    int32_t npu_core_count;
    float npu_tops;
    
    // GPU 信息
    char gpu_vendor[64];
    char gpu_renderer[64];
    char gpu_version[64];
    
    // 保留字段
    uint32_t reserved[16];
} RkHardwareInfo;

// ============================================
// 回调函数类型
// ============================================

// 进度回调 (progress: 0-100)
typedef void (*RkProgressCallback)(int progress, void* user_data);

// 错误回调
typedef void (*RkErrorCallback)(RkScreenshotError error, const char* message, void* user_data);

// 日志回调
typedef enum {
    RK_LOG_VERBOSE = 0,
    RK_LOG_DEBUG = 1,
    RK_LOG_INFO = 2,
    RK_LOG_WARN = 3,
    RK_LOG_ERROR = 4,
} RkLogLevel;

typedef void (*RkLogCallback)(RkLogLevel level, const char* tag, const char* message, void* user_data);

// ============================================
// C API
// ============================================

/**
 * 获取版本信息
 */
RK_API const char* rk_screenshot_get_version();

/**
 * 初始化截图引擎
 * @return RKSS_SUCCESS 成功，其他为错误码
 */
RK_API RkScreenshotError rk_screenshot_init();

/**
 * 初始化截图引擎 (高级版本，可指定配置)
 */
RK_API RkScreenshotError rk_screenshot_init_ex(const RkScreenshotConfig* config);

/**
 * 反初始化截图引擎
 */
RK_API void rk_screenshot_deinit();

/**
 * 查询硬件能力
 */
RK_API RkScreenshotError rk_screenshot_query_hardware(RkHardwareInfo* info);

/**
 * 获取默认配置
 */
RK_API void rk_screenshot_get_default_config(RkScreenshotConfig* config);

/**
 * 截图 (同步模式)
 * @param config 截图配置
 * @param result 输出结果，调用者需要调用 rk_screenshot_free_result 释放
 * @return RKSS_SUCCESS 成功，其他为错误码
 */
RK_API RkScreenshotError rk_screenshot_capture(
    const RkScreenshotConfig* config,
    RkScreenshotResult** result
);

/**
 * 截图 (异步模式)
 * @param config 截图配置
 * @param callback 完成回调
 * @param user_data 用户数据
 * @return 任务 ID (>= 0) 或错误码 (< 0)
 */
RK_API int rk_screenshot_capture_async(
    const RkScreenshotConfig* config,
    void (*callback)(RkScreenshotResult* result, void* user_data),
    void* user_data
);

/**
 * 取消异步任务
 */
RK_API RkScreenshotError rk_screenshot_cancel(int task_id);

/**
 * 等待异步任务完成
 */
RK_API RkScreenshotError rk_screenshot_wait(int task_id, int timeout_ms);

/**
 * 释放截图结果
 */
RK_API void rk_screenshot_free_result(RkScreenshotResult* result);

/**
 * 保存截图到文件
 * @param result 截图结果
 * @param filepath 文件路径
 * @return RKSS_SUCCESS 成功，其他为错误码
 */
RK_API RkScreenshotError rk_screenshot_save_to_file(
    const RkScreenshotResult* result,
    const char* filepath
);

/**
 * 设置日志回调
 */
RK_API void rk_screenshot_set_log_callback(
    RkLogCallback callback,
    void* user_data
);

/**
 * 设置日志级别
 */
RK_API void rk_screenshot_set_log_level(RkLogLevel level);

/**
 * 获取错误描述
 */
RK_API const char* rk_screenshot_error_string(RkScreenshotError error);

// ============================================
// 高级功能
// ============================================

/**
 * 添加水印
 * @param result 原图
 * @param watermark_data 水印图像数据 (RGBA)
 * @param wm_width 水印宽度
 * @param wm_height 水印高度
 * @param x 水印 x 坐标
 * @param y 水印 y 坐标
 * @param alpha 透明度 (0-255)
 */
RK_API RkScreenshotError rk_screenshot_add_watermark(
    RkScreenshotResult* result,
    const uint8_t* watermark_data,
    int32_t wm_width,
    int32_t wm_height,
    int32_t x,
    int32_t y,
    uint8_t alpha
);

/**
 * 批量截图
 * @param configs 配置数组
 * @param count 数量
 * @param results 输出结果数组
 * @return RKSS_SUCCESS 成功，其他为错误码
 */
RK_API RkScreenshotError rk_screenshot_capture_batch(
    const RkScreenshotConfig* configs,
    int count,
    RkScreenshotResult*** results
);

/**
 * 开始视频流录制
 * @param config 配置
 * @param filepath 输出文件路径
 * @return 录制 ID (>= 0) 或错误码 (< 0)
 */
RK_API int rk_screenshot_start_recording(
    const RkScreenshotConfig* config,
    const char* filepath
);

/**
 * 停止视频流录制
 */
RK_API RkScreenshotError rk_screenshot_stop_recording(int recording_id);

/**
 * NPU 图像增强
 */
RK_API RkScreenshotError rk_screenshot_npu_enhance(
    RkScreenshotResult* result,
    const char* model_path
);

#ifdef __cplusplus
}
#endif

// ============================================
// C++ API (封装类)
// ============================================
#ifdef __cplusplus

namespace rk {

class RK_API Screenshot {
public:
    Screenshot();
    ~Screenshot();
    
    // 禁止拷贝
    Screenshot(const Screenshot&) = delete;
    Screenshot& operator=(const Screenshot&) = delete;
    
    /**
     * 初始化
     */
    RkScreenshotError init();
    RkScreenshotError init(const RkScreenshotConfig& config);
    
    /**
     * 反初始化
     */
    void deinit();
    
    /**
     * 查询硬件信息
     */
    RkScreenshotError queryHardware(RkHardwareInfo& info);
    
    /**
     * 截图 (同步)
     */
    RkScreenshotError capture(const RkScreenshotConfig& config, RkScreenshotResult*& result);
    
    /**
     * 截图 (异步) - 使用 std::function
     */
    int captureAsync(
        const RkScreenshotConfig& config,
        std::function<void(RkScreenshotResult*)> callback
    );
    
    /**
     * 取消任务
     */
    RkScreenshotError cancel(int task_id);
    
    /**
     * 等待任务完成
     */
    RkScreenshotError wait(int task_id, int timeout_ms = -1);
    
    /**
     * 释放���果
     */
    void freeResult(RkScreenshotResult* result);
    
    /**
     * 保存到文件
     */
    RkScreenshotError saveToFile(const RkScreenshotResult* result, const std::string& filepath);
    
    /**
     * 设置日志回调
     */
    void setLogCallback(std::function<void(RkLogLevel, const std::string&, const std::string&)> callback);
    
    /**
     * 设置日志级别
     */
    void setLogLevel(RkLogLevel level);
    
    /**
     * 获取错误描述
     */
    static std::string getErrorString(RkScreenshotError error);
    
    /**
     * 获取默认配置
     */
    static RkScreenshotConfig getDefaultConfig();
    
    /**
     * 添加水印
     */
    RkScreenshotError addWatermark(
        RkScreenshotResult* result,
        const std::vector<uint8_t>& watermark,
        int wm_width, int wm_height,
        int x, int y, uint8_t alpha
    );
    
    /**
     * 批量截图
     */
    RkScreenshotError captureBatch(
        const std::vector<RkScreenshotConfig>& configs,
        std::vector<RkScreenshotResult*>& results
    );
    
    /**
     * 开始录制
     */
    int startRecording(const RkScreenshotConfig& config, const std::string& filepath);
    
    /**
     * 停止录制
     */
    RkScreenshotError stopRecording(int recording_id);
    
private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

/**
 * RAII 包装器 - 自动释放结果
 */
class RK_API ScreenshotResultGuard {
public:
    explicit ScreenshotResultGuard(RkScreenshotResult* result) : result_(result) {}
    
    ~ScreenshotResultGuard() {
        if (result_) {
            rk_screenshot_free_result(result_);
        }
    }
    
    RkScreenshotResult* get() const { return result_; }
    RkScreenshotResult* release() {
        RkScreenshotResult* tmp = result_;
        result_ = nullptr;
        return tmp;
    }
    
    // 禁止拷贝
    ScreenshotResultGuard(const ScreenshotResultGuard&) = delete;
    ScreenshotResultGuard& operator=(const ScreenshotResultGuard&) = delete;
    
    // 支持移动
    ScreenshotResultGuard(ScreenshotResultGuard&& other) noexcept : result_(other.result_) {
        other.result_ = nullptr;
    }
    
private:
    RkScreenshotResult* result_;
};

} // namespace rk

#endif // __cplusplus

#endif // RK3588_SCREENSHOT_H
