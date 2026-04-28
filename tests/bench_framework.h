/**
 * @file bench_framework.h
 * @brief Zero-config, auto-registering microbenchmark framework
 *
 * Single header. Works in C and C++. No manual main(), no manual registration.
 *
 *   #include "bench_framework.h"
 *
 *   BENCH(VectorPush) {
 *       std::vector<int> v;
 *       for (int i = 0; i < 1000; ++i) v.push_back(i);
 *   }
 *
 *   BENCH(Hash, FNV1a) {
 *       (void)fnv1a("hello world");
 *   }
 *
 * cforge auto-generates the main() when it builds a builtin-framework target,
 * so you do not need to write one. If you want one anyway:
 *
 *   int main(int argc, char** argv) { return cf_run_benches(argc, argv); }
 *
 * CLI accepted by cf_run_benches:
 *   --list                       Print bench names, one per line, then exit
 *   -f / --filter <glob>         Only run benches whose name matches the glob
 *   --min-time-ms <ms>           Per-bench wall-clock budget per sample (50)
 *   --samples <n>                Samples per bench (5)
 *   --no-color                   Disable ANSI colors
 *   <positional...>              Treated as additional --filter patterns
 *
 * Output is parsed by cforge's builtin benchmark adapter.
 *
 * SOURCE OF TRUTH: this file is mirrored byte-for-byte by an embedded copy in
 * src/core/utils/benchmark_runner.cpp. Update both together.
 */

#ifndef CFORGE_BENCH_FRAMEWORK_H
#define CFORGE_BENCH_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Need a high-resolution clock. */
#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#elif defined(__APPLE__)
  #include <mach/mach_time.h>
#else
  #include <time.h>
#endif

/* ============================================================================
 * Compiler attributes (mirror test_framework.h exactly so co-includes work)
 * ============================================================================ */

#if defined(__cplusplus)
#elif defined(_MSC_VER)
  #pragma section(".CRT$XCU", read)
  #ifdef _WIN64
    #define CFB_INCLUDE_PREFIX ""
  #else
    #define CFB_INCLUDE_PREFIX "_"
  #endif
  #define CFB_CTOR_(fn, line)                                                    \
      static void fn(void);                                                      \
      __pragma(section(".CRT$XCU",read))                                         \
      __declspec(allocate(".CRT$XCU")) void (*fn##_ptr_##line)(void) = fn;       \
      __pragma(comment(linker,"/include:" CFB_INCLUDE_PREFIX #fn "_ptr_" #line)) \
      static void fn(void)
  #define CFB_CTOR_X(fn, line) CFB_CTOR_(fn, line)
  #define CFB_CTOR(fn) CFB_CTOR_X(fn, __LINE__)
#elif defined(__GNUC__) || defined(__clang__)
  #define CFB_CTOR(fn) static void __attribute__((constructor)) fn(void)
#else
  #error "Unsupported compiler"
#endif

#if defined(_WIN32)
  #define CFB_SELECTANY __declspec(selectany)
#elif defined(_MSC_VER)
  #define CFB_SELECTANY __declspec(selectany)
#elif defined(__GNUC__) || defined(__clang__)
  #define CFB_SELECTANY __attribute__((weak))
#else
  #define CFB_SELECTANY
#endif

#if defined(__GNUC__) || defined(__clang__)
  #define CFB_MAYBE_UNUSED __attribute__((unused))
#else
  #define CFB_MAYBE_UNUSED
#endif

/* ============================================================================
 * Registry (C linkage so C and C++ TUs share the same symbol)
 * ============================================================================ */

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*cf_bench_func_t)(void);

typedef struct cf_bench_node {
    const char* name;
    cf_bench_func_t func;
    struct cf_bench_node* next;
} cf_bench_node_t;

CFB_SELECTANY cf_bench_node_t* cf_bench_head_ = (cf_bench_node_t*)0;
CFB_SELECTANY int cf_bench_color_ = 1;

static CFB_MAYBE_UNUSED void cf_bench_register_node_(cf_bench_node_t* node) {
    node->next = cf_bench_head_;
    cf_bench_head_ = node;
}

static CFB_MAYBE_UNUSED const char* cfb_red_(void)    { return cf_bench_color_ ? "\x1b[31m" : ""; }
static CFB_MAYBE_UNUSED const char* cfb_green_(void)  { return cf_bench_color_ ? "\x1b[32m" : ""; }
static CFB_MAYBE_UNUSED const char* cfb_cyan_(void)   { return cf_bench_color_ ? "\x1b[36m" : ""; }
static CFB_MAYBE_UNUSED const char* cfb_dim_(void)    { return cf_bench_color_ ? "\x1b[2m"  : ""; }
static CFB_MAYBE_UNUSED const char* cfb_reset_(void)  { return cf_bench_color_ ? "\x1b[0m"  : ""; }

#if defined(_WIN32)
static CFB_MAYBE_UNUSED void cfb_enable_vt_(void) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (h == INVALID_HANDLE_VALUE || h == NULL) { cf_bench_color_ = 0; return; }
    if (!GetConsoleMode(h, &mode)) return;
    if (!SetConsoleMode(h, mode | 0x0004)) cf_bench_color_ = 0;
}
#else
static CFB_MAYBE_UNUSED void cfb_enable_vt_(void) {}
#endif

/* High-resolution monotonic clock returning nanoseconds. */
static CFB_MAYBE_UNUSED double cf_bench_now_ns_(void) {
#if defined(_WIN32)
    static LARGE_INTEGER freq = {0};
    LARGE_INTEGER counter;
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart * 1e9 / (double)freq.QuadPart;
#elif defined(__APPLE__)
    static mach_timebase_info_data_t tb = {0, 0};
    if (tb.denom == 0) mach_timebase_info(&tb);
    return (double)mach_absolute_time() * (double)tb.numer / (double)tb.denom;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
#endif
}

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ============================================================================
 * BENCH macro
 *   BENCH(name)              -> registers as "name"
 *   BENCH(suite, name)       -> registers as "suite.name"
 * ============================================================================ */

#define CFB_CONCAT_(a, b) a##b
#define CFB_CONCAT(a, b) CFB_CONCAT_(a, b)

#ifdef __cplusplus

namespace cf_bench_internal {
struct Registrar {
    cf_bench_node_t node;
    Registrar(const char* name, cf_bench_func_t fn) {
        node.name = name;
        node.func = fn;
        node.next = nullptr;
        cf_bench_register_node_(&node);
    }
};
} /* namespace cf_bench_internal */

#define CFB_BENCH1(name)                                                         \
    static void name(void);                                                      \
    static ::cf_bench_internal::Registrar CFB_CONCAT(cfb_reg_, __LINE__)(#name, name); \
    static void name(void)

#define CFB_BENCH2(suite, name)                                                  \
    static void CFB_CONCAT(suite, CFB_CONCAT(_, name))(void);                    \
    static ::cf_bench_internal::Registrar CFB_CONCAT(cfb_reg_, __LINE__)(        \
        #suite "." #name, CFB_CONCAT(suite, CFB_CONCAT(_, name)));               \
    static void CFB_CONCAT(suite, CFB_CONCAT(_, name))(void)

#else /* C path: __attribute__((constructor)) or .CRT$XCU pointer */

#define CFB_BENCH1(name)                                                         \
    static void name(void);                                                      \
    static cf_bench_node_t CFB_CONCAT(cf_bnode_, name) = { #name, name, 0 };     \
    CFB_CTOR(CFB_CONCAT(cfb_reg_, name)) {                                       \
        cf_bench_register_node_(&CFB_CONCAT(cf_bnode_, name));                   \
    }                                                                            \
    static void name(void)

#define CFB_BENCH2(suite, name)                                                  \
    static void CFB_CONCAT(suite, CFB_CONCAT(_, name))(void);                    \
    static cf_bench_node_t                                                       \
        CFB_CONCAT(cf_bnode_, CFB_CONCAT(suite, CFB_CONCAT(_, name))) = {        \
            #suite "." #name,                                                    \
            CFB_CONCAT(suite, CFB_CONCAT(_, name)), 0 };                         \
    CFB_CTOR(CFB_CONCAT(cfb_reg_, CFB_CONCAT(suite, CFB_CONCAT(_, name)))) {     \
        cf_bench_register_node_(                                                 \
            &CFB_CONCAT(cf_bnode_, CFB_CONCAT(suite, CFB_CONCAT(_, name))));     \
    }                                                                            \
    static void CFB_CONCAT(suite, CFB_CONCAT(_, name))(void)

#endif

#define CFB_EXPAND(x) x
#define CFB_GET_MACRO(_1, _2, NAME, ...) NAME
#define CFB_BENCH_CHOOSER(...) CFB_EXPAND(CFB_GET_MACRO(__VA_ARGS__, CFB_BENCH2, CFB_BENCH1))
#define BENCH(...) CFB_EXPAND(CFB_BENCH_CHOOSER(__VA_ARGS__)(__VA_ARGS__))

/* Stop the optimizer from eliding the benchmark body when the body computes a
 * value that's never used. cf_clobber_() is a memory barrier; users may also
 * write `volatile <T> sink = expr;` to keep specific values alive. */
#if defined(__GNUC__) || defined(__clang__)
  static CFB_MAYBE_UNUSED void cf_clobber_(void) { __asm__ __volatile__("" ::: "memory"); }
#elif defined(_MSC_VER)
  #include <intrin.h>
  #pragma intrinsic(_ReadWriteBarrier)
  static CFB_MAYBE_UNUSED void cf_clobber_(void) { _ReadWriteBarrier(); }
#else
  static CFB_MAYBE_UNUSED void cf_clobber_(void) {}
#endif

/* ============================================================================
 * Glob match (same shape as test framework's, but separate symbol).
 * ============================================================================ */

static CFB_MAYBE_UNUSED int cfb_glob_match_(const char* pat, const char* s) {
    while (*pat && *s) {
        if (*pat == '*') {
            while (*pat == '*') pat++;
            if (*pat == 0) return 1;
            while (*s) {
                if (cfb_glob_match_(pat, s)) return 1;
                s++;
            }
            return 0;
        } else if (*pat == '?' || *pat == *s) {
            pat++; s++;
        } else {
            return 0;
        }
    }
    while (*pat == '*') pat++;
    return *pat == 0 && *s == 0;
}

static CFB_MAYBE_UNUSED int cfb_filter_matches_(const char* name,
                                                const char* const* filters, int filter_count) {
    int i; size_t flen;
    if (filter_count <= 0) return 1;
    for (i = 0; i < filter_count; i++) {
        const char* f = filters[i];
        if (!f) continue;
        if (cfb_glob_match_(f, name)) return 1;
        if (strchr(f, '*') == 0 && strchr(f, '?') == 0) {
            if (strcmp(f, name) == 0) return 1;
            flen = strlen(f);
            if (strncmp(f, name, flen) == 0 && name[flen] == '.') return 1;
            if (strstr(name, f) != 0) return 1;
        }
    }
    return 0;
}

/* ============================================================================
 * Runner: adaptive iteration count, multiple samples, statistics.
 * ============================================================================ */

#ifdef __cplusplus
extern "C" {
#endif

/* Reverse the head-prepended linked list to declaration order. */
static CFB_MAYBE_UNUSED cf_bench_node_t* cfb_collect_in_order_(int* out_count) {
    cf_bench_node_t* prev = (cf_bench_node_t*)0;
    cf_bench_node_t* curr = cf_bench_head_;
    int count = 0;
    while (curr) {
        cf_bench_node_t* next = curr->next;
        curr->next = prev;
        prev = curr;
        curr = next;
        count++;
    }
    cf_bench_head_ = prev;
    if (out_count) *out_count = count;
    return prev;
}

/* Tiny portable sqrt — bench framework should not pull in <math.h> for C. */
static CFB_MAYBE_UNUSED double cfb_sqrt_(double x) {
    double r;
    int i;
    if (x <= 0) return 0;
    r = x;
    for (i = 0; i < 16; i++) r = 0.5 * (r + x / r);
    return r;
}

/* Compute (mean, stddev, min, max) over n samples in array `times`. */
static CFB_MAYBE_UNUSED void cfb_stats_(const double* times, int n,
                                        double* mean, double* stddev,
                                        double* tmin, double* tmax) {
    int i;
    double sum = 0, sum_sq = 0;
    *tmin = times[0]; *tmax = times[0];
    for (i = 0; i < n; i++) {
        sum += times[i];
        if (times[i] < *tmin) *tmin = times[i];
        if (times[i] > *tmax) *tmax = times[i];
    }
    *mean = sum / n;
    for (i = 0; i < n; i++) {
        double d = times[i] - *mean;
        sum_sq += d * d;
    }
    *stddev = (n > 1) ? cfb_sqrt_(sum_sq / (n - 1)) : 0.0;
}

static CFB_MAYBE_UNUSED int cf_run_benches(int argc, char** argv) {
    const char* filters[64];
    int filter_count = 0;
    int list_only = 0;
    double min_time_ms = 50.0;  /* per-sample budget */
    int samples = 5;
    int i;
    int total = 0;
    int ok = 0, failed = 0, skipped = 0;
    cf_bench_node_t* head;
    cf_bench_node_t* n;

    cfb_enable_vt_();

    for (i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (strcmp(a, "--list") == 0) list_only = 1;
        else if (strcmp(a, "--no-color") == 0) cf_bench_color_ = 0;
        else if ((strcmp(a, "-f") == 0 || strcmp(a, "--filter") == 0) &&
                 i + 1 < argc && filter_count < 64) {
            filters[filter_count++] = argv[++i];
        }
        else if (strcmp(a, "--min-time-ms") == 0 && i + 1 < argc) {
            min_time_ms = atof(argv[++i]);
            if (min_time_ms < 1.0) min_time_ms = 1.0;
        }
        else if (strcmp(a, "--samples") == 0 && i + 1 < argc) {
            samples = atoi(argv[++i]);
            if (samples < 1) samples = 1;
            if (samples > 64) samples = 64;
        }
        else if (a[0] != '-' && filter_count < 64) {
            filters[filter_count++] = a;
        }
    }

    head = cfb_collect_in_order_(&total);

    if (list_only) {
        for (n = head; n; n = n->next) {
            if (cfb_filter_matches_(n->name, filters, filter_count)) {
                printf("%s\n", n->name);
            }
        }
        fflush(stdout);
        return 0;
    }

    printf("\n");
    for (n = head; n; n = n->next) {
        double iter_times[64];
        long long iters_per_sample;
        double t0, t1, elapsed_ns;
        double mean, stddev, tmin, tmax;
        int s;

        if (!cfb_filter_matches_(n->name, filters, filter_count)) {
            skipped++;
            continue;
        }
        if (!n->func) { failed++; continue; }

        printf("%s[BRUN]%s %s\n", cfb_cyan_(), cfb_reset_(), n->name);
        fflush(stdout);

        /* Warmup: ensure caches/JIT/branch predictors are settled. */
        n->func();

        /* Estimate iters_per_sample so a single sample takes ~min_time_ms. */
        iters_per_sample = 1;
        for (;;) {
            long long k;
            t0 = cf_bench_now_ns_();
            for (k = 0; k < iters_per_sample; k++) n->func();
            t1 = cf_bench_now_ns_();
            elapsed_ns = t1 - t0;
            if (elapsed_ns >= min_time_ms * 1e6 || iters_per_sample >= (1LL << 30)) {
                break;
            }
            /* Scale by ratio of target/observed, with a safety multiplier. */
            if (elapsed_ns < 1000.0) {
                iters_per_sample *= 100;
            } else {
                double scale = (min_time_ms * 1e6) / elapsed_ns;
                if (scale < 2.0) scale = 2.0;
                iters_per_sample = (long long)((double)iters_per_sample * scale);
                if (iters_per_sample < 1) iters_per_sample = 1;
            }
        }

        /* Now run `samples` measurements, recording ns/iter for each. */
        if (samples > 64) samples = 64;
        for (s = 0; s < samples; s++) {
            long long k;
            t0 = cf_bench_now_ns_();
            for (k = 0; k < iters_per_sample; k++) n->func();
            t1 = cf_bench_now_ns_();
            iter_times[s] = (t1 - t0) / (double)iters_per_sample;
        }

        cfb_stats_(iter_times, samples, &mean, &stddev, &tmin, &tmax);

        /* Machine-parseable line — keep stable for the cforge bench adapter. */
        printf("[BENCH] %s iters=%lld mean_ns=%.3f stddev_ns=%.3f min_ns=%.3f max_ns=%.3f\n",
               n->name,
               (long long)iters_per_sample,
               mean, stddev, tmin, tmax);

        printf("%s[BOK]%s   %s  %s%.2f ns/op%s  (n=%lld, %ssd=%.2f%s)\n",
               cfb_green_(), cfb_reset_(), n->name,
               cfb_dim_(), mean, cfb_reset_(),
               (long long)iters_per_sample,
               cfb_dim_(), stddev, cfb_reset_());
        fflush(stdout);
        ok++;
    }

    printf("\n==============================\n");
    if (failed == 0) {
        printf("%sRan %d benchmarks%s%s%s\n",
               cfb_green_(), ok, cfb_reset_(),
               skipped ? " (" : "",
               "");
        if (skipped) printf("%s%d filtered out%s\n", cfb_dim_(), skipped, cfb_reset_());
    } else {
        printf("%s%d of %d benchmarks failed%s\n",
               cfb_red_(), failed, ok + failed, cfb_reset_());
    }
    printf("==============================\n");
    fflush(stdout);

    return failed > 0 ? 1 : 0;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CFORGE_BENCH_FRAMEWORK_H */
