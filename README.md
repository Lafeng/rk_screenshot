# RK3588 Screenshot

**高性能 Android 截图库** — 基于 SurfaceFlinger + DMA-BUF + RGA + MPP 全硬件加速管线

专为 RK3588 (Android 13+) 优化，**充分利用 SoC 全部硬件加速单元**（ISP/RGA/VPU/DMA），实现 **78+ FPS** JPEG 实时截图、**115+ FPS** Raw 捕获。

---

## Performance Benchmark

> 🔬 RK3588 真机测试数据 (50 iterations average)

| 场景 | 分辨率 | 帧时间 | FPS | 吞吐量 | 对比 screencap |
|------|--------|--------|-----|--------|----------------|
| Raw RGBA | 1920×1080 | 8.65ms | **115.6** | 959 MB/s | **11.6× 更快** |
| JPEG Full | 1920×1080 | 12.72ms | **78.6** | 22.5 MB/s | **7.9× 更快** |
| JPEG 720p | 1280×720 | 16.81ms | **59.5** | 8.8 MB/s | **6.0× 更快** |
| Thumbnail | 320×180 | 8.72ms | **114.6** | 2.2 MB/s | **11.5× 更快** |

> 📊 对比基准：Android `screencap -p` 约 100ms/帧

### 为什么这么快？

| 阶段 | screencap | rk_screenshot |
|------|-----------|---------------|
| 屏幕捕获 | SurfaceFlinger → CPU copy | SurfaceFlinger → **DMA-BUF zero-copy** |
| 图像缩放 | CPU (skia) | **RGA 2D 硬件加速** |
| JPEG 编码 | CPU (libjpeg) | **MPP 硬件编码器** |
| 内存拷贝 | 3-4 次 | **0-1 次** |

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                     rk_screenshot Library                       │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   ┌───────────────┐    ┌─────────────┐    ┌─────────────┐       │
│   │ SurfaceFlinger│    │     RGA     │    │     MPP     │       │
│   │    Capture    │───▶│  Processor  │───▶│   Encoder   │       │
│   │    (5-10ms)   │    │   (2-4ms)   │    │  (5-15ms)   │       │
│   └───────────────┘    └─────────────┘    └─────────────┘       │
│          │                  │                  │                │
│          ▼                  ▼                  ▼                │
│   ┌─────────────────────────────────────────────────────┐       │
│   │                DMA-BUF Zero-Copy Path               │       │
│   │      GraphicBuffer ──▶ DMA-HEAP ──▶ MPP Buffer      │       │
│   └─────────────────────────────────────────────────────┘       │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 设计亮点

#### 1. Pure DMA-BUF Pipeline
- **无 CPU 拷贝**：从 SurfaceFlinger 到 JPEG 输出，数据始终在 GPU/VPU 可访问的 DMA 内存中
- **硬件直通**：RGA 和 MPP 通过 IOMMU 直接访问 DMA-BUF，无需 mmap 到用户空间

#### 2. 智能 MPP 编码模式
```
宽高都 16 对齐？
    ├── YES → ZERO-COPY (mpp_buffer_import)
    └── NO  → MEMCPY (安全处理边界)
```
- 1280×720、1920×1080 等标准分辨率享受零拷贝
- 非对齐尺寸自动降级，保证稳定性

#### 3. SurfaceFlinger AIDL (Android 13+)
- 使用 `SyncScreenCaptureListener` 同步等待
- `ProcessState::startThreadPool()` 确保 Binder 回调可达
- 直接获取 `GraphicBuffer` 的 DMA-BUF fd

#### 4. RGA wrapbuffer_fd 模式
- 绕过 RK3588 的 4GB MMU 限制
- 通过 IOMMU 访问，支持任意物理地址

---

## Source Structure

```
src/
├── rk_screenshot.cpp              # Public C API + 生命周期管理
├── rk_surfaceflinger_capture.cpp  # SurfaceFlinger 捕获 (Binder + AIDL)
├── rk_rga_processor.cpp           # RGA 2D 缩放/旋转
├── rk_mpp_encoder.cpp             # MPP JPEG 编码 (智能模式)
└── rk_dmabuf_utils.cpp            # /dev/dma_heap 分配器

include/
├── rk_screenshot.h                # Public API
└── rk_internal.h                  # 内部结构体

test/
└── rk_screenshot_test.cpp         # 功能 + 性能测试套件

tools/
└── rk_screenshot.cpp              # 命令行工具
```

---

## Build (AOSP Soong)

> ⚠️ **必须使用 AOSP 编译**，NDK 无法访问 SurfaceFlinger API

### 编译
```bash
cd ~/aosp
source build/envsetup.sh
lunch <your_target>

# Link 项目到 AOSP 树
ln -sf /path/to/rk_screenshot external/rk_screenshot

# 编译
m librk_screenshot rk_screenshot_test rk_screenshot
```

### 部署
```bash
# 库文件
adb push out/.../system/lib64/librk_screenshot.so /system/lib64/

# 工具
adb push out/.../system/bin/rk_screenshot_test /data/local/tmp/
adb push out/.../system/bin/rk_screenshot /data/local/tmp/
```

---

## Usage

### 命令行工具: `rk_screenshot`

```bash
# 基础截图
rk_screenshot output.jpg

# 指定尺寸 (RGA 硬件缩放)
rk_screenshot -s 1280x720 thumb.jpg

# 高质量
rk_screenshot -q 95 hq.jpg

# Raw RGBA 输出
rk_screenshot -r screen.rgba

# Pipe 模式 (输出到 stdout)
rk_screenshot | base64 > screenshot.b64

# 显示耗时
rk_screenshot -t -v output.jpg
```

**Options:**
| 参数 | 说明 |
|------|------|
| `-s WxH` | 缩放到指定尺寸 |
| `-q N` | JPEG 质量 1-100 (默认 90) |
| `-r` | 输出 Raw RGBA8888 |
| `-t` | 显示各阶段耗时 |
| `-v` | 详细输出 |

### 测试工具: `rk_screenshot_test`

```bash
# 功能测试 (5 个测试用例)
rk_screenshot_test -f

# 性能测试 (100 次迭代)
rk_screenshot_test -p 100

# Benchmark 模式 (无文件 I/O)
rk_screenshot_test -b 100
```

**输出示例:**
```
🔥 JPEG 720p (1280×720):
   Iterations: 100
   Time: min=20.12ms, max=25.67ms, avg=21.89ms
   FPS: 45.7
   Size: avg=147KB
```

---

## C API

```cpp
#include "rk_screenshot.h"

// 初始化 (一次)
rk_screenshot_init();

// 配置
RkScreenshotConfig cfg;
rk_screenshot_get_default_config(&cfg);
cfg.format = RK_FORMAT_JPEG;
cfg.quality = 90;
cfg.scale_width = 1280;
cfg.scale_height = 720;

// 截图
RkScreenshotResult* result = NULL;
RkScreenshotError err = rk_screenshot_capture(&cfg, &result);

if (err == RKSS_SUCCESS) {
    // result->data = JPEG 数据
    // result->size = 数据大小
    // result->width, result->height = 实际尺寸
    
    save_file("output.jpg", result->data, result->size);
    rk_screenshot_free_result(result);
}

// 清理 (一次)
rk_screenshot_deinit();
```

---

## Dependencies

### AOSP System Libraries
| 库 | 用途 |
|----|------|
| `libgui` | SurfaceFlinger 客户端 |
| `libui` | GraphicBuffer |
| `libbinder` | Binder IPC |
| `libutils` | Android 工具类 |
| `liblog` | Android 日志 |
| `libnativewindow` | Native window 管理 |

### Rockchip Prebuilt Libraries

> ⚠️ **Rockchip 专有库不包含在此仓库**，需从设备或 SDK 获取

**目录结构:**
```
prebuilts/
└── arm64/
    ├── librga.so          # RGA 2D 硬件加速库
    └── libmpp.so          # MPP 媒体处理库

include/
├── mpp/                   # MPP 头文件 (25+ files)
│   ├── mpp_buffer.h
│   ├── mpp_frame.h
│   ├── rk_mpi.h
│   └── ...
└── librga/                # RGA 头文件
    ├── rga.h
    ├── im2d.h
    └── ...
```

**获取方式:**

1. **从设备提取:**
   ```bash
   adb pull /system/lib64/librga.so prebuilts/arm64/
   adb pull /system/lib64/libmpp.so prebuilts/arm64/
   ```

2. **从 Rockchip SDK:**
   - 下载 RK3588 Android SDK
   - 复制 `librga.so` 和 `libmpp.so` 到 `prebuilts/arm64/`
   - 复制头文件到 `include/mpp/` 和 `include/librga/`

3. **从 AOSP 编译输出:**
   ```bash
   cp ~/aosp/out/.../system/lib64/librga.so prebuilts/arm64/
   cp ~/aosp/out/.../system/lib64/libmpp.so prebuilts/arm64/
   ```

---

## Limitations

- **Android 13+ only** — 使用 AIDL 版本的 SurfaceFlinger API
- **需要 system 权限** — 访问 SurfaceFlinger 需要签名或 root
- **仅支持 JPEG** — 暂不支持 PNG/WebP (MPP 硬件限制)
- **仅支持主屏** — 多屏截图需扩展 display ID 参数

---

## License

Apache 2.0
