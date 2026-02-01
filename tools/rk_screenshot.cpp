/**
 * RK3588 Screenshot Tool
 * 
 * 高性能屏幕截图命令行工具
 * 
 * Usage:
 *   rk_screencap                    # 输出到 stdout (JPEG)
 *   rk_screencap output.jpg         # 保存 JPEG
 *   rk_screencap output.png         # 保存 PNG (if supported)
 *   rk_screencap -r output.rgba     # 保存原始 RGBA
 *   rk_screencap -s 1280x720 out.jpg  # 缩放到指定尺寸
 *   rk_screencap -q 85 out.jpg      # 指定 JPEG 质量 (1-100)
 */

#include "rk_screenshot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/time.h>

//==============================================================================
// Configuration
//==============================================================================

typedef struct {
    const char* output_file;
    RkImageFormat format;
    int quality;
    int scale_width;
    int scale_height;
    bool verbose;
    bool to_stdout;
    bool show_timing;
} AppConfig;

static void init_config(AppConfig* cfg) {
    cfg->output_file = NULL;
    cfg->format = RK_FORMAT_JPEG;
    cfg->quality = 90;
    cfg->scale_width = 0;
    cfg->scale_height = 0;
    cfg->verbose = false;
    cfg->to_stdout = false;
    cfg->show_timing = false;
}

//==============================================================================
// Utilities
//==============================================================================

static uint64_t get_time_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;
}

static bool ends_with(const char* str, const char* suffix) {
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    if (str_len < suffix_len) return false;
    return strcasecmp(str + str_len - suffix_len, suffix) == 0;
}

static RkImageFormat detect_format(const char* filename) {
    if (ends_with(filename, ".rgba") || ends_with(filename, ".raw")) {
        return RK_FORMAT_RGBA8888;
    }
    return RK_FORMAT_JPEG;  // Default
}

//==============================================================================
// Main
//==============================================================================

static void print_usage(const char* prog) {
    fprintf(stderr, "RK3588 Screenshot Tool v2.0\n\n");
    fprintf(stderr, "Usage: %s [options] [output_file]\n\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -s WxH       Scale to specified size (e.g., -s 1280x720)\n");
    fprintf(stderr, "  -q QUALITY   JPEG quality 1-100 (default: 90)\n");
    fprintf(stderr, "  -r           Output raw RGBA8888 format\n");
    fprintf(stderr, "  -v           Verbose output (to stderr)\n");
    fprintf(stderr, "  -t           Show timing information\n");
    fprintf(stderr, "  -h           Show this help\n");
    fprintf(stderr, "\nOutput:\n");
    fprintf(stderr, "  If output_file is specified, write to file\n");
    fprintf(stderr, "  Otherwise, write JPEG to stdout (for piping)\n");
    fprintf(stderr, "\nExamples:\n");
    fprintf(stderr, "  %s screenshot.jpg              # Save JPEG\n", prog);
    fprintf(stderr, "  %s -s 1280x720 thumb.jpg       # Scaled JPEG\n", prog);
    fprintf(stderr, "  %s -q 95 -v hq.jpg             # High quality with verbose\n", prog);
    fprintf(stderr, "  %s | base64                    # Pipe JPEG to base64\n", prog);
    fprintf(stderr, "  %s -r screen.rgba              # Raw RGBA data\n", prog);
}

static bool parse_size(const char* str, int* width, int* height) {
    const char* x = strchr(str, 'x');
    if (!x) x = strchr(str, 'X');
    if (!x) return false;
    
    *width = atoi(str);
    *height = atoi(x + 1);
    return (*width > 0 && *height > 0);
}

int main(int argc, char** argv) {
    AppConfig cfg;
    init_config(&cfg);
    
    // Parse options
    int opt;
    while ((opt = getopt(argc, argv, "s:q:rvth")) != -1) {
        switch (opt) {
            case 's':
                if (!parse_size(optarg, &cfg.scale_width, &cfg.scale_height)) {
                    fprintf(stderr, "Error: Invalid size format '%s', use WxH\n", optarg);
                    return 1;
                }
                break;
            case 'q':
                cfg.quality = atoi(optarg);
                if (cfg.quality < 1 || cfg.quality > 100) {
                    fprintf(stderr, "Error: Quality must be 1-100\n");
                    return 1;
                }
                break;
            case 'r':
                cfg.format = RK_FORMAT_RGBA8888;
                break;
            case 'v':
                cfg.verbose = true;
                break;
            case 't':
                cfg.show_timing = true;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    // Get output file
    if (optind < argc) {
        cfg.output_file = argv[optind];
        // Auto-detect format from extension
        if (cfg.format != RK_FORMAT_RGBA8888) {
            cfg.format = detect_format(cfg.output_file);
        }
    } else {
        cfg.to_stdout = true;
        cfg.format = RK_FORMAT_JPEG;  // stdout always JPEG
    }
    
    // Check if stdout is a terminal
    if (cfg.to_stdout && isatty(STDOUT_FILENO)) {
        fprintf(stderr, "Error: Will not write binary data to terminal.\n");
        fprintf(stderr, "Specify an output file or redirect stdout.\n");
        return 1;
    }
    
    uint64_t t_start = get_time_us();
    
    // Initialize
    RkScreenshotError err = rk_screenshot_init();
    if (err != RKSS_SUCCESS) {
        fprintf(stderr, "Error: Init failed: %s\n", rk_screenshot_error_string(err));
        return 1;
    }
    
    uint64_t t_init = get_time_us();
    
    // Configure capture
    RkScreenshotConfig cap_cfg;
    rk_screenshot_get_default_config(&cap_cfg);
    cap_cfg.format = cfg.format;
    cap_cfg.quality = cfg.quality;
    cap_cfg.scale_width = cfg.scale_width;
    cap_cfg.scale_height = cfg.scale_height;
    
    if (cfg.verbose) {
        fprintf(stderr, "Config: format=%s, quality=%d, scale=%dx%d\n",
                cfg.format == RK_FORMAT_JPEG ? "JPEG" : "RGBA",
                cfg.quality, cfg.scale_width, cfg.scale_height);
    }
    
    // Capture
    RkScreenshotResult* result = NULL;
    err = rk_screenshot_capture(&cap_cfg, &result);
    
    uint64_t t_capture = get_time_us();
    
    if (err != RKSS_SUCCESS || !result) {
        fprintf(stderr, "Error: Capture failed: %s\n", rk_screenshot_error_string(err));
        rk_screenshot_deinit();
        return 1;
    }
    
    // Output
    if (cfg.to_stdout) {
        // Write to stdout
        size_t written = fwrite(result->data, 1, result->size, stdout);
        if (written != result->size) {
            fprintf(stderr, "Error: Write failed\n");
            rk_screenshot_free_result(result);
            rk_screenshot_deinit();
            return 1;
        }
    } else {
        // Write to file
        FILE* f = fopen(cfg.output_file, "wb");
        if (!f) {
            fprintf(stderr, "Error: Cannot open '%s' for writing\n", cfg.output_file);
            rk_screenshot_free_result(result);
            rk_screenshot_deinit();
            return 1;
        }
        
        size_t written = fwrite(result->data, 1, result->size, f);
        fclose(f);
        
        if (written != result->size) {
            fprintf(stderr, "Error: Write failed\n");
            rk_screenshot_free_result(result);
            rk_screenshot_deinit();
            return 1;
        }
        
        if (cfg.verbose) {
            fprintf(stderr, "Saved: %s (%zu bytes)\n", cfg.output_file, result->size);
        }
    }
    
    uint64_t t_write = get_time_us();
    
    // Timing info
    if (cfg.show_timing || cfg.verbose) {
        fprintf(stderr, "Resolution: %dx%d\n", result->width, result->height);
        fprintf(stderr, "Size: %zu bytes (%.1f KB)\n", result->size, result->size / 1024.0);
    }
    
    if (cfg.show_timing) {
        fprintf(stderr, "Timing:\n");
        fprintf(stderr, "  Init:    %.2f ms\n", (t_init - t_start) / 1000.0);
        fprintf(stderr, "  Capture: %.2f ms\n", (t_capture - t_init) / 1000.0);
        fprintf(stderr, "  Write:   %.2f ms\n", (t_write - t_capture) / 1000.0);
        fprintf(stderr, "  Total:   %.2f ms\n", (t_write - t_start) / 1000.0);
    }
    
    // Cleanup
    rk_screenshot_free_result(result);
    rk_screenshot_deinit();
    
    return 0;
}
