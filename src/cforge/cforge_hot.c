/**
 * @file cforge_hot.c
 * @brief Hot reload runtime library implementation (pure C, no external deps)
 *
 * Cross-platform shared library hot-swapping via a signal file written by
 * `cforge hot`.  The host reads the signal file each frame and reloads when
 * the counter increases.
 *
 * Platform support:
 *   Windows : LoadLibraryA / FreeLibrary / GetProcAddress
 *   POSIX   : dlopen / dlclose / dlsym  (-ldl)
 *
 * Windows DLL locking workaround: before loading, the rebuilt DLL is copied
 * to a versioned name (e.g. game_hot_003.dll).  The new copy is loaded, then
 * the old handle is released.  Stale copies are cleaned up on unload.
 */

#include "cforge/cforge_hot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Platform abstraction layer
 * ========================================================================= */

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>

typedef HMODULE lib_handle_t;

static lib_handle_t platform_load(const char *path) {
    return LoadLibraryA(path);
}

static void platform_unload(lib_handle_t h) {
    if (h) FreeLibrary(h);
}

static void *platform_sym(lib_handle_t h, const char *name) {
    if (!h) return NULL;
    return (void *)GetProcAddress(h, name);
}

/* Copy src -> dst.  Retries up to max_retries times with delay_ms sleep. */
static int platform_copy_file(const char *src, const char *dst,
                               int max_retries, int delay_ms) {
    int attempt;
    for (attempt = 0; attempt < max_retries; ++attempt) {
        if (CopyFileA(src, dst, FALSE)) return 1;
        if (attempt + 1 < max_retries) Sleep((DWORD)delay_ms);
    }
    return 0;
}

static void platform_delete_file(const char *path) {
    DeleteFileA(path);
}

static long long platform_mtime(const char *path) {
    HANDLE fh = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (fh == INVALID_HANDLE_VALUE) return -1;
    FILETIME ft;
    long long result = -1;
    if (GetFileTime(fh, NULL, NULL, &ft)) {
        result = (long long)ft.dwHighDateTime << 32 | (long long)ft.dwLowDateTime;
    }
    CloseHandle(fh);
    return result;
}

static void platform_sleep_ms(int ms) {
    Sleep((DWORD)ms);
}

/* Return directory separator */
#define PATH_SEP '\\'

#else /* POSIX */

#  include <dlfcn.h>
#  include <sys/stat.h>
#  include <fcntl.h>
#  include <unistd.h>
#  include <time.h>
#  include <errno.h>

typedef void *lib_handle_t;

static lib_handle_t platform_load(const char *path) {
    /* RTLD_LOCAL: don't pollute global symbol table. */
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}

static void platform_unload(lib_handle_t h) {
    if (h) dlclose(h);
}

static void *platform_sym(lib_handle_t h, const char *name) {
    if (!h) return NULL;
    return dlsym(h, name);
}

static int copy_file_posix(const char *src, const char *dst) {
    FILE *in_f  = fopen(src, "rb");
    FILE *out_f = NULL;
    char buf[65536];
    size_t n;
    int ok = 0;

    if (!in_f) return 0;
    out_f = fopen(dst, "wb");
    if (!out_f) { fclose(in_f); return 0; }

    while ((n = fread(buf, 1, sizeof(buf), in_f)) > 0) {
        if (fwrite(buf, 1, n, out_f) != n) goto done;
    }
    ok = 1;

done:
    fclose(in_f);
    fclose(out_f);
    return ok;
}

static int platform_copy_file(const char *src, const char *dst,
                               int max_retries, int delay_ms) {
    int attempt;
    (void)delay_ms; /* POSIX files don't lock like Windows DLLs */
    for (attempt = 0; attempt < max_retries; ++attempt) {
        if (copy_file_posix(src, dst)) {
            /* Make executable */
            chmod(dst, 0755);
            return 1;
        }
        if (attempt + 1 < max_retries) usleep((useconds_t)delay_ms * 1000);
    }
    return 0;
}

static void platform_delete_file(const char *path) {
    unlink(path);
}

static long long platform_mtime(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
#ifdef __APPLE__
    return (long long)st.st_mtime * 1000000000LL + st.st_mtimespec.tv_nsec;
#else
    return (long long)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec;
#endif
}

static void platform_sleep_ms(int ms) {
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

#define PATH_SEP '/'

#endif /* _WIN32 */

/* =========================================================================
 * Thread-local error string
 * ========================================================================= */

#ifdef _WIN32
static __declspec(thread) char tl_error[512];
#else
static __thread char tl_error[512];
#endif

static void set_error(const char *msg) {
    if (msg) {
        strncpy(tl_error, msg, sizeof(tl_error) - 1);
        tl_error[sizeof(tl_error) - 1] = '\0';
    } else {
        tl_error[0] = '\0';
    }
}

const char *cforge_hot_last_error(void) {
    return tl_error;
}

/* =========================================================================
 * Internal struct definition
 * ========================================================================= */

struct cforge_hot_ctx {
    char library_path[1024];   /* Original (canonical) library path       */
    char loaded_path[1024];    /* Currently-loaded versioned copy path    */
    char lib_dir[1024];        /* Directory containing the library        */
    char base_name[256];       /* Base name without extension (e.g. game) */
    char lib_ext[16];          /* Extension (.dll / .so / .dylib)         */
    lib_handle_t handle;       /* OS library handle                       */
    int  version;              /* Monotonic reload counter (starts at 1)  */
    char signal_path[1024];    /* .cforge/hot_reload_signal file          */
    long long last_signal;     /* Last seen signal counter value          */
};

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

#ifndef CFORGE_HOT_POLLING_MS
#  define CFORGE_HOT_POLLING_MS 50
#endif

/* Determine the library directory from an absolute library path. */
static void extract_dir_and_base(const char *library_path,
                                  char *dir_out,  size_t dir_max,
                                  char *base_out, size_t base_max,
                                  char *ext_out,  size_t ext_max) {
    size_t len = strlen(library_path);
    size_t last_sep = 0;
    size_t dot_pos  = len; /* index of last dot */
    size_t i;

    for (i = 0; i < len; ++i) {
        if (library_path[i] == '/' || library_path[i] == '\\') last_sep = i + 1;
        if (library_path[i] == '.') dot_pos = i;
    }

    /* dir */
    if (last_sep > 0 && last_sep <= dir_max - 1) {
        strncpy(dir_out, library_path, last_sep);
        dir_out[last_sep] = '\0';
    } else {
        strncpy(dir_out, ".", dir_max - 1);
        dir_out[dir_max - 1] = '\0';
    }

    /* base (no extension) */
    if (dot_pos > last_sep) {
        size_t base_len = dot_pos - last_sep;
        if (base_len >= base_max) base_len = base_max - 1;
        strncpy(base_out, library_path + last_sep, base_len);
        base_out[base_len] = '\0';
    } else {
        strncpy(base_out, library_path + last_sep, base_max - 1);
        base_out[base_max - 1] = '\0';
    }

    /* extension (including dot) */
    if (dot_pos < len) {
        strncpy(ext_out, library_path + dot_pos, ext_max - 1);
        ext_out[ext_max - 1] = '\0';
    } else {
        ext_out[0] = '\0';
    }
}

/*
 * Determine the signal file path.
 *
 * The library typically lives in <project>/<build_dir>/lib/.
 * We walk up two parent directories from lib_dir to find the project root,
 * then append .cforge/hot_reload_signal.
 *
 * If the project root can't be determined we fall back to
 * <lib_dir>/../../.cforge/hot_reload_signal.
 */
static void build_signal_path(const char *lib_dir, char *out, size_t out_max) {
    char tmp[1024];
    size_t len = strlen(lib_dir);

    /* Strip trailing separators */
    strncpy(tmp, lib_dir, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    while (len > 1 && (tmp[len - 1] == '/' || tmp[len - 1] == '\\')) {
        tmp[--len] = '\0';
    }

    /* Go up two levels: lib/ -> build/ -> project/ */
    int levels = 2;
    while (levels-- > 0 && len > 0) {
        while (len > 0 && tmp[len - 1] != '/' && tmp[len - 1] != '\\') --len;
        if (len > 0) {
            /* also strip the separator */
            --len;
        }
        tmp[len] = '\0';
    }

    if (len == 0) {
        /* Fallback: put signal alongside library */
        strncpy(tmp, lib_dir, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
    }

#ifdef _WIN32
    snprintf(out, out_max, "%s\\.cforge\\hot_reload_signal", tmp);
#else
    snprintf(out, out_max, "%s/.cforge/hot_reload_signal", tmp);
#endif
}

/* Build a versioned copy path: <dir>/<base>_hot_<NNN><ext> */
static void versioned_path(const struct cforge_hot_ctx *ctx, int ver,
                            char *out, size_t out_max) {
#ifdef _WIN32
    snprintf(out, out_max, "%s%s_hot_%03d%s",
             ctx->lib_dir, ctx->base_name, ver, ctx->lib_ext);
#else
    snprintf(out, out_max, "%s%s_hot_%03d%s",
             ctx->lib_dir, ctx->base_name, ver, ctx->lib_ext);
#endif
}

/* Read the signal file and return the counter value, or -1 on error. */
static long long read_signal_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    long long val = -1;
    fscanf(f, "%lld", &val);
    fclose(f);
    return val;
}

/*
 * Clean up stale versioned copies in lib_dir that match <base>_hot_NNN<ext>.
 * We allow up to 5 stale copies before deleting; on unload we delete all.
 */
static void cleanup_versioned_copies(const struct cforge_hot_ctx *ctx,
                                      int keep_version) {
    int v;
    char path[1024];
    for (v = 1; v <= ctx->version + 5; ++v) {
        if (v == keep_version) continue;
        versioned_path(ctx, v, path, sizeof(path));
        platform_delete_file(path); /* silently ignore errors */
    }
}

/* =========================================================================
 * Public API implementation
 * ========================================================================= */

cforge_hot_ctx *cforge_hot_load(const char *library_path) {
    cforge_hot_ctx *ctx;
    char copy_path[1024];

    if (!library_path) {
        set_error("library_path is NULL");
        return NULL;
    }

    ctx = (cforge_hot_ctx *)calloc(1, sizeof(cforge_hot_ctx));
    if (!ctx) {
        set_error("out of memory");
        return NULL;
    }

    strncpy(ctx->library_path, library_path, sizeof(ctx->library_path) - 1);

    extract_dir_and_base(library_path,
                          ctx->lib_dir,   sizeof(ctx->lib_dir),
                          ctx->base_name, sizeof(ctx->base_name),
                          ctx->lib_ext,   sizeof(ctx->lib_ext));

    build_signal_path(ctx->lib_dir, ctx->signal_path, sizeof(ctx->signal_path));

    /* Prime last_signal from the file (if it already exists) so we don't
       trigger an immediate reload on first call to cforge_hot_reload(). */
    ctx->last_signal = read_signal_file(ctx->signal_path);

    /* Copy to versioned name before loading (avoids Windows file-lock) */
    ctx->version = 1;
    versioned_path(ctx, ctx->version, copy_path, sizeof(copy_path));

    if (!platform_copy_file(library_path, copy_path, 3, 10)) {
        /* If copy fails, try loading the original directly (POSIX is fine) */
        strncpy(copy_path, library_path, sizeof(copy_path) - 1);
        copy_path[sizeof(copy_path) - 1] = '\0';
    }

    ctx->handle = platform_load(copy_path);
    if (!ctx->handle) {
#ifdef _WIN32
        char err_buf[256];
        DWORD code = GetLastError();
        snprintf(err_buf, sizeof(err_buf), "LoadLibraryA failed (error %lu): %s",
                 code, copy_path);
        set_error(err_buf);
#else
        set_error(dlerror());
#endif
        free(ctx);
        return NULL;
    }

    strncpy(ctx->loaded_path, copy_path, sizeof(ctx->loaded_path) - 1);
    set_error("");
    return ctx;
}

int cforge_hot_reload(cforge_hot_ctx *ctx) {
    long long signal_val;
    char new_copy[1024];
    lib_handle_t new_handle;
    lib_handle_t old_handle;
    int new_version;

    if (!ctx) { set_error("ctx is NULL"); return -1; }

    signal_val = read_signal_file(ctx->signal_path);
    if (signal_val < 0) return 0;          /* file absent or unreadable */
    if (signal_val <= ctx->last_signal) return 0;  /* no new build */

    /* New build available — copy canonical library to a new versioned name */
    new_version = ctx->version + 1;
    versioned_path(ctx, new_version, new_copy, sizeof(new_copy));

    if (!platform_copy_file(ctx->library_path, new_copy, 3, 10)) {
        char err_buf[512];
        snprintf(err_buf, sizeof(err_buf),
                 "failed to copy '%s' -> '%s' (retried 3 times)",
                 ctx->library_path, new_copy);
        set_error(err_buf);
        return -1;
    }

    new_handle = platform_load(new_copy);
    if (!new_handle) {
#ifdef _WIN32
        DWORD code = GetLastError();
        char err_buf[256];
        snprintf(err_buf, sizeof(err_buf),
                 "LoadLibraryA failed (error %lu): %s", code, new_copy);
        set_error(err_buf);
#else
        set_error(dlerror());
#endif
        platform_delete_file(new_copy);
        return -1;
    }

    /* Atomically swap handles */
    old_handle = ctx->handle;
    ctx->handle = new_handle;
    strncpy(ctx->loaded_path, new_copy, sizeof(ctx->loaded_path) - 1);
    ctx->last_signal = signal_val;
    ctx->version = new_version;

    /* Release old library after swap */
    platform_unload(old_handle);

    /* Clean up previous versioned copies (keep current) */
    cleanup_versioned_copies(ctx, ctx->version);

    set_error("");
    return 1;
}

void *cforge_hot_get_symbol(cforge_hot_ctx *ctx, const char *symbol_name) {
    if (!ctx || !symbol_name) return NULL;
    return platform_sym(ctx->handle, symbol_name);
}

int cforge_hot_get_version(cforge_hot_ctx *ctx) {
    if (!ctx) return 0;
    return ctx->version;
}

void cforge_hot_watch(cforge_hot_ctx *ctx, void (*on_reload)(cforge_hot_ctx *)) {
    if (!ctx) return;

    for (;;) {
        int rc = cforge_hot_reload(ctx);
        if (rc == 1) {
            /* Successfully reloaded */
            if (on_reload) on_reload(ctx);
        } else if (rc == -1) {
            /* Unrecoverable or transient error — keep running */
        }

        /* Exit when signal file is gone (cforge hot was stopped) */
        {
            FILE *f = fopen(ctx->signal_path, "r");
            if (!f) break;
            fclose(f);
        }

        platform_sleep_ms(CFORGE_HOT_POLLING_MS);
    }
}

void cforge_hot_unload(cforge_hot_ctx *ctx) {
    if (!ctx) return;

    platform_unload(ctx->handle);
    ctx->handle = NULL;

    /* Remove all versioned copies */
    cleanup_versioned_copies(ctx, -1);  /* keep_version=-1 deletes all */

    free(ctx);
}
