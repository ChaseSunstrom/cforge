/**
 * @file cforge_hot.h
 * @brief Hot reload runtime library for cforge
 *
 * Pure C header — safe to include from C and C++ projects.
 *
 * Typical host application usage:
 *
 *   #include <cforge/cforge_hot.h>
 *
 *   cforge_hot_ctx *ctx = cforge_hot_load("build/lib/game.so");
 *   while (running) {
 *       cforge_hot_reload(ctx);  // non-blocking
 *       update_fn fn = (update_fn)cforge_hot_get_symbol(ctx, "game_update");
 *       if (fn) fn(dt);
 *   }
 *   cforge_hot_unload(ctx);
 */

#ifndef CFORGE_HOT_H
#define CFORGE_HOT_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque hot-reload context.
 *
 * Allocated by cforge_hot_load(), freed by cforge_hot_unload().
 */
typedef struct cforge_hot_ctx cforge_hot_ctx;

/**
 * @brief Load a shared library and return an opaque context.
 *
 * On Windows, copies the library to a versioned name before loading to avoid
 * file-locking issues.  The signal file is expected at
 * <library_parent>/../.cforge/hot_reload_signal (i.e. one level above the
 * build/lib/ directory that contains the .dll/.so).
 *
 * @param library_path  Path to the compiled shared library (.dll / .so / .dylib).
 * @return Pointer to a newly allocated cforge_hot_ctx, or NULL on failure.
 *         Call cforge_hot_last_error() for a human-readable error string.
 */
cforge_hot_ctx *cforge_hot_load(const char *library_path);

/**
 * @brief Check the signal file for a new build and reload if one is available.
 *
 * This is non-blocking and safe to call every frame.  On reload the old
 * library handle is released *after* the new one is loaded, so the old symbols
 * remain valid until this function returns.
 *
 * @param ctx  Context returned by cforge_hot_load().
 * @return  1 if the library was reloaded,
 *          0 if nothing changed,
 *         -1 on error (the old library remains loaded).
 */
int cforge_hot_reload(cforge_hot_ctx *ctx);

/**
 * @brief Look up a symbol in the currently loaded library.
 *
 * Re-call after every successful cforge_hot_reload() because the function
 * pointer address changes when the library is swapped.
 *
 * @param ctx          Context returned by cforge_hot_load().
 * @param symbol_name  Name of the exported symbol (use extern "C" in the module).
 * @return Pointer to the symbol, or NULL if not found.
 */
void *cforge_hot_get_symbol(cforge_hot_ctx *ctx, const char *symbol_name);

/**
 * @brief Return the number of times the library has been loaded (starts at 1).
 *
 * @param ctx  Context returned by cforge_hot_load().
 * @return     Reload counter (monotonically increasing).
 */
int cforge_hot_get_version(cforge_hot_ctx *ctx);

/**
 * @brief Blocking helper: poll the signal file and reload automatically.
 *
 * Calls on_reload() after each successful reload.  Returns when the signal
 * file is deleted or an unrecoverable error occurs.  Polls at the interval
 * defined by CFORGE_HOT_POLLING_MS (default 50 ms).
 *
 * @param ctx        Context returned by cforge_hot_load().
 * @param on_reload  Callback invoked after each successful reload (may be NULL).
 */
void cforge_hot_watch(cforge_hot_ctx *ctx, void (*on_reload)(cforge_hot_ctx *));

/**
 * @brief Return a human-readable string for the last error.
 *
 * The string is stored in thread-local storage and is valid until the next
 * call that may produce an error on the same thread.
 *
 * @return Null-terminated error string (never NULL, may be empty).
 */
const char *cforge_hot_last_error(void);

/**
 * @brief Unload the library, delete versioned copies, and free the context.
 *
 * Safe to call with NULL.
 *
 * @param ctx  Context to destroy.
 */
void cforge_hot_unload(cforge_hot_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif /* CFORGE_HOT_H */
