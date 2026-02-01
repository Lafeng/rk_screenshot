/**
 * RK3588 Screenshot Test Suite
 * 
 * 功能测试 + 性能测试入口
 * 
 * Usage:
 *   test_screenshot              # 运行全部测试
 *   test_screenshot -f           # 仅功能测试
 *   test_screenshot -p [count]   # 性能测试 (默认100次)
 *   test_screenshot -b [count]   # 纯性能基准测试 (无文件IO)
 */

#include "../include/rk_screenshot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

//==============================================================================
// Utilities
//==============================================================================

static uint64_t get_time_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;
}

static void save_file(const char* filename, const void* data, size_t size) {
    FILE* f = fopen(filename, "wb");
    if (f) {
        fwrite(data, 1, size, f);
        fclose(f);
        printf("   💾 Saved: %s (%zu bytes)\n", filename, size);
    } else {
        printf("   ❌ Failed to save: %s\n", filename);
    }
}

static void print_separator(const char* title) {
    printf("\n════════════════════════════════════════════════════════════\n");
    if (title) printf("  %s\n", title);
    printf("════════════════════════════════════════════════════════════\n");
}

//==============================================================================
// Functional Tests
//==============================================================================

typedef struct {
    const char* name;
    const char* filename;
    RkImageFormat format;
    int quality;
    int scale_width;
    int scale_height;
} TestCase;

#define NUM_TEST_CASES 5
static TestCase g_test_cases[NUM_TEST_CASES] = {
    {"Raw RGBA (1920x1080)",      "test_raw.rgba",    RK_FORMAT_RGBA8888, 0,  0, 0},
    {"JPEG Full (1920x1080 Q90)", "test_full.jpg",    RK_FORMAT_JPEG,     90, 0, 0},
    {"JPEG Scaled (1280x720 Q85)","test_scaled.jpg",  RK_FORMAT_JPEG,     85, 1280, 720},
    {"Thumbnail (320x180 Q75)",   "test_thumb.jpg",   RK_FORMAT_JPEG,     75, 320, 180},
    {"HD Ready (1280x720 Q90)",   "test_720p.jpg",    RK_FORMAT_JPEG,     90, 1280, 720},
};

static int run_functional_tests(bool save_files) {
    print_separator("🧪 FUNCTIONAL TESTS");
    
    int passed = 0;
    int total = NUM_TEST_CASES;
    
    for (int i = 0; i < total; i++) {
        TestCase* tc = &g_test_cases[i];
        printf("\n📷 Test %d/%d: %s\n", i + 1, total, tc->name);
        
        RkScreenshotConfig cfg;
        rk_screenshot_get_default_config(&cfg);
        cfg.format = tc->format;
        cfg.quality = tc->quality;
        cfg.scale_width = tc->scale_width;
        cfg.scale_height = tc->scale_height;
        
        RkScreenshotResult* res = NULL;
        uint64_t t0 = get_time_us();
        RkScreenshotError err = rk_screenshot_capture(&cfg, &res);
        uint64_t elapsed = get_time_us() - t0;
        
        if (err == RKSS_SUCCESS && res) {
            printf("   ✅ Success: %dx%d, %zu bytes, %.2f ms\n", 
                   res->width, res->height, res->size, elapsed / 1000.0);
            
            if (save_files) {
                save_file(tc->filename, res->data, res->size);
            }
            
            rk_screenshot_free_result(res);
            passed++;
        } else {
            printf("   ❌ Failed: %s\n", rk_screenshot_error_string(err));
        }
    }
    
    printf("\n────────────────────────────────────────────────────────────\n");
    printf("📊 Result: %d/%d tests passed\n", passed, total);
    
    return (passed == total) ? 0 : 1;
}

//==============================================================================
// Performance Tests
//==============================================================================

typedef struct {
    const char* name;
    RkImageFormat format;
    int quality;
    int scale_width;
    int scale_height;
} PerfTestCase;

#define NUM_PERF_TESTS 4
static PerfTestCase g_perf_tests[NUM_PERF_TESTS] = {
    {"Raw RGBA",    RK_FORMAT_RGBA8888, 0,  0, 0},
    {"JPEG 1080p",  RK_FORMAT_JPEG,     90, 0, 0},
    {"JPEG 720p",   RK_FORMAT_JPEG,     85, 1280, 720},
    {"Thumbnail",   RK_FORMAT_JPEG,     75, 320, 180},
};

static void run_performance_tests(int iterations, bool benchmark_mode) {
    print_separator(benchmark_mode ? "⚡ BENCHMARK MODE" : "📈 PERFORMANCE TESTS");
    printf("  Iterations: %d\n", iterations);
    
    int num_tests = NUM_PERF_TESTS;
    
    for (int t = 0; t < num_tests; t++) {
        PerfTestCase* tc = &g_perf_tests[t];
        printf("\n🔥 %s:\n", tc->name);
        
        RkScreenshotConfig cfg;
        rk_screenshot_get_default_config(&cfg);
        cfg.format = tc->format;
        cfg.quality = tc->quality;
        cfg.scale_width = tc->scale_width;
        cfg.scale_height = tc->scale_height;
        
        uint64_t total_time = 0;
        uint64_t min_time = UINT64_MAX;
        uint64_t max_time = 0;
        size_t total_bytes = 0;
        int success_count = 0;
        
        // Warmup
        for (int i = 0; i < 3; i++) {
            RkScreenshotResult* res = NULL;
            if (rk_screenshot_capture(&cfg, &res) == RKSS_SUCCESS) {
                rk_screenshot_free_result(res);
            }
        }
        
        // Actual test
        for (int i = 0; i < iterations; i++) {
            RkScreenshotResult* res = NULL;
            uint64_t t0 = get_time_us();
            RkScreenshotError err = rk_screenshot_capture(&cfg, &res);
            uint64_t elapsed = get_time_us() - t0;
            
            if (err == RKSS_SUCCESS && res) {
                total_time += elapsed;
                if (elapsed < min_time) min_time = elapsed;
                if (elapsed > max_time) max_time = elapsed;
                total_bytes += res->size;
                success_count++;
                rk_screenshot_free_result(res);
            }
            
            if (!benchmark_mode && (i + 1) % 10 == 0) {
                printf("   Progress: %d/%d\r", i + 1, iterations);
                fflush(stdout);
            }
        }
        
        if (success_count > 0) {
            double avg_ms = (total_time / success_count) / 1000.0;
            double fps = 1000.0 / avg_ms;
            double throughput_mbps = (total_bytes / success_count) / avg_ms / 1000.0;
            
            printf("   ✅ %d/%d successful\n", success_count, iterations);
            printf("   ⏱️  Time: avg=%.2f ms, min=%.2f ms, max=%.2f ms\n",
                   avg_ms, min_time / 1000.0, max_time / 1000.0);
            printf("   🚀 FPS: %.1f\n", fps);
            printf("   📊 Avg size: %.1f KB, Throughput: %.1f MB/s\n",
                   (total_bytes / success_count) / 1024.0, throughput_mbps);
        } else {
            printf("   ❌ All iterations failed!\n");
        }
    }
}

//==============================================================================
// Main
//==============================================================================

static void print_usage(const char* prog) {
    printf("Usage: %s [options]\n", prog);
    printf("\nOptions:\n");
    printf("  -f           Functional tests only (with file output)\n");
    printf("  -p [count]   Performance tests (default: 100 iterations)\n");
    printf("  -b [count]   Benchmark mode (no progress output)\n");
    printf("  -h           Show this help\n");
    printf("\nNo options: Run both functional and performance tests\n");
}

int main(int argc, char** argv) {
    bool run_func = false;
    bool run_perf = false;
    bool benchmark = false;
    int iterations = 100;
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0) {
            run_func = true;
        } else if (strcmp(argv[i], "-p") == 0) {
            run_perf = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                iterations = atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "-b") == 0) {
            run_perf = true;
            benchmark = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                iterations = atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }
    
    // Default: run all
    if (!run_func && !run_perf) {
        run_func = true;
        run_perf = true;
        iterations = 50;  // Shorter for combined mode
    }
    
    print_separator("🚀 RK3588 Screenshot Test Suite v2.0");
    
    // Initialize
    RkScreenshotError err = rk_screenshot_init();
    if (err != RKSS_SUCCESS) {
        printf("❌ Init failed: %s\n", rk_screenshot_error_string(err));
        return 1;
    }
    
    int result = 0;
    
    if (run_func) {
        result = run_functional_tests(true);
    }
    
    if (run_perf) {
        run_performance_tests(iterations, benchmark);
    }
    
    // Cleanup
    rk_screenshot_deinit();
    
    print_separator("✅ Test Suite Complete");
    
    return result;
}
