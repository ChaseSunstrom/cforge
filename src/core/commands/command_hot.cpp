/**
 * @file command_hot.cpp
 * @brief Implementation of the `cforge hot` command (hot reload session)
 *
 * Starts a hot reload session:
 *   1. Reads [hot_reload] from cforge.toml, errors with helpful message if absent.
 *   2. Performs an initial build of the shared library module.
 *   3. Launches the host executable as a child process.
 *   4. Enters a file-watch loop.  On source change:
 *      a. Rebuilds the shared library.
 *      b. On success: atomically writes an incremented counter to
 *         .cforge/hot_reload_signal.
 *      c. On failure: logs the error, keeps old library running.
 *   5. On Ctrl-C: terminates the host process, deletes the signal file.
 */

// Prevent Windows min/max macros from conflicting with std::min/max
#if defined(_WIN32) && !defined(NOMINMAX)
#  define NOMINMAX
#endif

#include "cforge/log.hpp"
#include "core/build_utils.hpp"
#include "core/command_registry.hpp"
#include "core/commands.hpp"
#include "core/constants.h"
#include "core/process_utils.hpp"
#include "core/toml_reader.hpp"
#include "core/types.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

namespace fs = std::filesystem;

// ============================================================================
// Anonymous helpers shared within this translation unit
// ============================================================================

namespace {

// Global graceful-shutdown flag (shared with signal handlers)
std::atomic<bool> g_hot_should_exit{false};

// PID of the spawned host process (0 = not running)
#ifdef _WIN32
HANDLE g_host_proc_handle = INVALID_HANDLE_VALUE;
#else
pid_t g_host_pid = 0;
#endif

// -------------------------------------------------------------------------
// Signal handling
// -------------------------------------------------------------------------

#ifdef _WIN32
BOOL WINAPI hot_console_handler(DWORD ctrl_type) {
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT) {
        g_hot_should_exit = true;
        return TRUE;
    }
    return FALSE;
}
#else
void hot_signal_handler(int) { g_hot_should_exit = true; }
#endif

void setup_signal_handlers() {
#ifdef _WIN32
    SetConsoleCtrlHandler(hot_console_handler, TRUE);
#else
    signal(SIGINT,  hot_signal_handler);
    signal(SIGTERM, hot_signal_handler);
#endif
}

// -------------------------------------------------------------------------
// Host process management
// -------------------------------------------------------------------------

/** Launch the host executable as a detached child.  Returns true on success. */
bool launch_host(const std::string &exe_path,
                 const std::string &working_dir) {
#ifdef _WIN32
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    // CreateProcess modifies the command-line buffer
    std::string cmd_line = "\"" + exe_path + "\"";
    if (!CreateProcessA(NULL, cmd_line.data(), NULL, NULL, FALSE,
                         CREATE_NEW_PROCESS_GROUP, NULL,
                         working_dir.empty() ? NULL : working_dir.c_str(),
                         &si, &pi)) {
        cforge::logger::print_error("Failed to launch host: " + exe_path);
        return false;
    }
    CloseHandle(pi.hThread);
    g_host_proc_handle = pi.hProcess;
    return true;
#else
    pid_t pid = fork();
    if (pid < 0) {
        cforge::logger::print_error("fork() failed while launching host: " + exe_path);
        return false;
    }
    if (pid == 0) {
        // Child
        if (!working_dir.empty()) {
            if (chdir(working_dir.c_str()) != 0) {
                _exit(1);
            }
        }
        execl(exe_path.c_str(), exe_path.c_str(), (char *)NULL);
        _exit(127); // exec failed
    }
    g_host_pid = pid;
    return true;
#endif
}

/** Check whether the host process is still alive.  Returns true if alive. */
bool host_is_alive() {
#ifdef _WIN32
    if (g_host_proc_handle == INVALID_HANDLE_VALUE) return false;
    DWORD code = 0;
    if (!GetExitCodeProcess(g_host_proc_handle, &code)) return false;
    return code == STILL_ACTIVE;
#else
    if (g_host_pid <= 0) return false;
    int status = 0;
    pid_t r = waitpid(g_host_pid, &status, WNOHANG);
    if (r == g_host_pid) { g_host_pid = 0; return false; }
    return true;
#endif
}

/** Terminate the host process gracefully (SIGTERM / TerminateProcess). */
void terminate_host() {
#ifdef _WIN32
    if (g_host_proc_handle != INVALID_HANDLE_VALUE) {
        TerminateProcess(g_host_proc_handle, 0);
        WaitForSingleObject(g_host_proc_handle, 2000);
        CloseHandle(g_host_proc_handle);
        g_host_proc_handle = INVALID_HANDLE_VALUE;
    }
#else
    if (g_host_pid > 0) {
        kill(g_host_pid, SIGTERM);
        int status;
        waitpid(g_host_pid, &status, 0);
        g_host_pid = 0;
    }
#endif
}

// -------------------------------------------------------------------------
// Signal file
// -------------------------------------------------------------------------

/**
 * Atomically write an incremented counter to .cforge/hot_reload_signal.
 * Uses write-to-tmp-then-rename for atomicity.
 */
bool write_signal_file(const fs::path &signal_path, long long counter) {
    fs::path tmp_path = signal_path;
    tmp_path += ".tmp";

    std::ofstream ofs(tmp_path);
    if (!ofs.is_open()) {
        cforge::logger::print_error("Cannot write signal file: " + tmp_path.string());
        return false;
    }
    ofs << counter << "\n";
    ofs.close();

    std::error_code ec;
    fs::rename(tmp_path, signal_path, ec);
    if (ec) {
        cforge::logger::print_error("Cannot rename signal file: " + ec.message());
        return false;
    }
    return true;
}

void delete_signal_file(const fs::path &signal_path) {
    std::error_code ec;
    fs::remove(signal_path, ec);
}

// -------------------------------------------------------------------------
// Minimal file watcher (reuses the polling approach from command_watch.cpp)
// -------------------------------------------------------------------------

struct FileWatcher {
    fs::path root;
    std::vector<std::string> extensions;
    std::map<fs::path, fs::file_time_type> files;

    FileWatcher(const fs::path &r, const std::vector<std::string> &ext)
        : root(r), extensions(ext) {
        scan_all();
    }

    bool check_for_changes() {
        bool changed = false;
        std::vector<fs::path> to_remove;

        for (auto &[p, t] : files) {
            if (!fs::exists(p)) {
                to_remove.push_back(p);
                changed = true;
                continue;
            }
            auto nt = fs::last_write_time(p);
            if (nt != t) { t = nt; changed = true; }
        }
        for (auto &p : to_remove) files.erase(p);

        // Scan for new files
        try {
            for (auto &entry : fs::recursive_directory_iterator(root)) {
                if (!entry.is_regular_file()) continue;
                auto ps = entry.path().string();
                if (ps.find("build") != std::string::npos ||
                    ps.find(".git")  != std::string::npos ||
                    ps.find("deps")  != std::string::npos ||
                    ps.find("vendor") != std::string::npos) continue;
                auto ext = entry.path().extension().string();
                bool valid = false;
                for (auto &e : extensions) if (ext == e) { valid = true; break; }
                if (valid && files.find(entry.path()) == files.end()) {
                    files[entry.path()] = fs::last_write_time(entry.path());
                    changed = true;
                }
            }
        } catch (...) {}

        return changed;
    }

private:
    void scan_all() {
        if (!fs::exists(root)) return;
        try {
            for (auto &entry : fs::recursive_directory_iterator(root)) {
                if (!entry.is_regular_file()) continue;
                auto ps = entry.path().string();
                if (ps.find("build") != std::string::npos ||
                    ps.find(".git")  != std::string::npos ||
                    ps.find("deps")  != std::string::npos ||
                    ps.find("vendor") != std::string::npos) continue;
                auto ext = entry.path().extension().string();
                for (auto &e : extensions) {
                    if (ext == e) {
                        files[entry.path()] = fs::last_write_time(entry.path());
                        break;
                    }
                }
            }
        } catch (...) {}
    }
};

// -------------------------------------------------------------------------
// Build helper
// -------------------------------------------------------------------------

bool run_hot_build(const fs::path &project_dir,
                   const std::string &config,
                   bool verbose) {
    auto start = std::chrono::steady_clock::now();

    fs::path build_dir = cforge::get_build_dir_for_config(
        (project_dir / DEFAULT_BUILD_DIR).string(), config, true);

    auto prep = cforge::prepare_project_for_build(
        project_dir, build_dir, config, verbose,
        false,  // toml_changed
        false   // force_reconfigure
    );

    if (!prep.success) {
        cforge::logger::print_error(prep.error_message);
        return false;
    }

    bool ok = cforge::run_cmake_build(build_dir, config, "", 0, verbose);

    auto end = std::chrono::steady_clock::now();
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    if (ok) {
        cforge::logger::finished(config, fmt::format("{:.2f}s", ms.count() / 1000.0));
    }
    return ok;
}

} // anonymous namespace

// ============================================================================
// Public command entry point
// ============================================================================

cforge_int_t cforge_cmd_hot(const cforge_context_t *ctx) {
    // Help flag
    for (cforge_int_t i = 0; i < ctx->args.arg_count; ++i) {
        std::string arg = ctx->args.args[i];
        if (arg == "-h" || arg == "--help") {
            cforge::command_registry::instance().print_command_help("hot");
            return 0;
        }
    }

    fs::path project_dir = ctx->working_dir;

    // Parse arguments
    std::string config;
    bool verbose = false;
    cforge_int_t poll_interval_ms = 500;

    for (cforge_int_t i = 0; i < ctx->args.arg_count; ++i) {
        std::string arg = ctx->args.args[i];
        if ((arg == "-c" || arg == "--config") && i + 1 < ctx->args.arg_count) {
            config = ctx->args.args[++i];
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg == "--interval" && i + 1 < ctx->args.arg_count) {
            poll_interval_ms = std::stoi(ctx->args.args[++i]);
        } else if (arg == "--release") {
            config = "Release";
        } else if (arg == "--debug") {
            config = "Debug";
        }
    }

    // Locate cforge.toml
    fs::path config_file = project_dir / "cforge.toml";
    if (!fs::exists(config_file)) {
        cforge::logger::print_error("No cforge.toml found in current directory");
        return 1;
    }

    // Load TOML
    cforge::toml_reader reader;
    if (!reader.load(config_file.string())) {
        cforge::logger::print_error("Failed to parse cforge.toml");
        return 1;
    }

    // Require [hot_reload] section
    if (!reader.has_key("hot_reload.enabled")) {
        cforge::logger::print_error("No [hot_reload] section found in cforge.toml");
        cforge::logger::print_blank();
        cforge::logger::print_plain("  Add the following to your cforge.toml:");
        cforge::logger::print_blank();
        cforge::logger::print_config_block({
            "[hot_reload]",
            "enabled     = true",
            "host        = \"src/host_main.cpp\"   # compiled as executable",
            "module      = \"src/game.cpp\"         # compiled as shared library",
            "entry_point = \"game_update\"           # optional: symbol to verify",
            "watch_dirs  = [\"src\", \"include\"]    # defaults to [build].source_dirs",
        });
        return 1;
    }

    bool hr_enabled = reader.get_bool("hot_reload.enabled", false);
    if (!hr_enabled) {
        cforge::logger::print_warning("[hot_reload] is present but enabled = false");
        return 1;
    }

    std::string hr_host        = reader.get_string("hot_reload.host");
    std::string hr_module      = reader.get_string("hot_reload.module");
    std::string hr_entry_point = reader.get_string("hot_reload.entry_point");
    std::vector<std::string> hr_watch_dirs =
        reader.get_string_array("hot_reload.watch_dirs");

    if (hr_host.empty()) {
        cforge::logger::print_error("hot_reload.host is required in cforge.toml");
        return 1;
    }
    if (hr_module.empty()) {
        cforge::logger::print_error("hot_reload.module is required in cforge.toml");
        return 1;
    }

    // Warn if binary_type != shared_lib (hot reload requires a shared library)
    std::string binary_type = reader.get_string("project.binary_type");
    if (!binary_type.empty() &&
        binary_type != "shared_lib" && binary_type != "shared_library") {
        cforge::logger::print_warning(
            "binary_type = \"" + binary_type +
            "\" — hot reload requires binary_type = \"shared_lib\"");
        cforge::logger::print_hint(
            "Set binary_type = \"shared_lib\" in [project] for the module target");
    }

    // Default watch dirs to [build].source_dirs
    if (hr_watch_dirs.empty()) {
        hr_watch_dirs = reader.get_string_array("build.source_dirs");
        if (hr_watch_dirs.empty()) hr_watch_dirs = {"src", "include"};
    }

    // Effective build config
    if (config.empty()) config = "Debug";

    // Project name
    std::string project_name = reader.get_string("project.name", "project");

    // -----------------------------------------------------------------------
    // Create .cforge/ directory and prepare signal file path
    // -----------------------------------------------------------------------

    fs::path cforge_dir  = project_dir / ".cforge";
    fs::path signal_path = cforge_dir  / "hot_reload_signal";

    std::error_code fs_ec;
    fs::create_directories(cforge_dir, fs_ec);
    if (fs_ec) {
        cforge::logger::print_error("Cannot create .cforge/ directory: " + fs_ec.message());
        return 1;
    }

    // Clean up any stale signal file from a previous session
    delete_signal_file(signal_path);

    // -----------------------------------------------------------------------
    // Initial build
    // -----------------------------------------------------------------------

    cforge::logger::building(project_name + " (shared library)");
    if (!run_hot_build(project_dir, config, verbose)) {
        cforge::logger::print_error("Initial build failed — aborting hot reload session");
        return 1;
    }

    // -----------------------------------------------------------------------
    // Determine host executable path and module library path
    // -----------------------------------------------------------------------

    fs::path build_dir = cforge::get_build_dir_for_config(
        (project_dir / DEFAULT_BUILD_DIR).string(), config, false);

    fs::path host_exe = cforge::find_project_binary(
        build_dir, project_name, config, "executable");
    if (host_exe.empty() || !fs::exists(host_exe)) {
        // Fallback: look for any executable named like the host source stem
        fs::path host_stem = fs::path(hr_host).stem();
        host_exe = cforge::find_project_binary(
            build_dir, host_stem.string(), config, "executable");
    }

    // Determine shared library path
    fs::path module_lib;
    {
        fs::path lib_dir = build_dir / "lib";
        if (!fs::exists(lib_dir)) lib_dir = build_dir;
#ifdef _WIN32
        module_lib = lib_dir / (project_name + ".dll");
        if (!fs::exists(module_lib))
            module_lib = lib_dir / (project_name + ".dll");
#elif defined(__APPLE__)
        module_lib = lib_dir / ("lib" + project_name + ".dylib");
        if (!fs::exists(module_lib))
            module_lib = lib_dir / (project_name + ".dylib");
#else
        module_lib = lib_dir / ("lib" + project_name + ".so");
        if (!fs::exists(module_lib))
            module_lib = lib_dir / (project_name + ".so");
#endif
    }

    // -----------------------------------------------------------------------
    // Print "Hot reload ready" banner
    // -----------------------------------------------------------------------

    cforge::logger::print_blank();
    cforge::logger::print_action("Watching", hr_watch_dirs[0] + " (hot-reload active)");
    cforge::logger::print_blank();
    cforge::logger::print_kv("host",   host_exe.empty()  ? "(not found)" : host_exe.string());
    cforge::logger::print_kv("module", module_lib.empty() ? "(not found)" : module_lib.string());
    if (!hr_entry_point.empty()) {
        cforge::logger::print_kv("entry_point", hr_entry_point);
    }
    cforge::logger::print_blank();
    cforge::logger::print_dim("Press Ctrl+C to stop");
    cforge::logger::print_blank();

    // -----------------------------------------------------------------------
    // Launch host executable
    // -----------------------------------------------------------------------

    if (!host_exe.empty() && fs::exists(host_exe)) {
        cforge::logger::running(host_exe.filename().string());
        if (!launch_host(host_exe.string(), project_dir.string())) {
            cforge::logger::print_warning(
                "Failed to launch host; continuing in watch-only mode");
        }
    } else {
        cforge::logger::print_warning(
            "Host executable not found; continuing in watch-only mode");
    }

    // -----------------------------------------------------------------------
    // Set up file watcher
    // -----------------------------------------------------------------------

    setup_signal_handlers();

    std::vector<std::string> watch_extensions = {
        ".cpp", ".cc", ".cxx", ".c", ".hpp", ".hxx", ".h"
    };

    // Build list of roots to watch
    std::vector<FileWatcher> watchers;
    for (auto &wd : hr_watch_dirs) {
        fs::path watch_root = project_dir / wd;
        if (fs::exists(watch_root)) {
            watchers.emplace_back(watch_root, watch_extensions);
        }
    }
    if (watchers.empty()) {
        // Fall back to project root
        watchers.emplace_back(project_dir, watch_extensions);
    }

    long long signal_counter = 0;

    // -----------------------------------------------------------------------
    // Watch loop
    // -----------------------------------------------------------------------

    while (!g_hot_should_exit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));

        // Check if host died unexpectedly
        if (!host_is_alive() && !g_hot_should_exit) {
#ifdef _WIN32
            if (g_host_proc_handle != INVALID_HANDLE_VALUE) {
#else
            if (g_host_pid != 0) {
#endif
                cforge::logger::print_error(
                    "Host process exited unexpectedly; "
                    "will relaunch on next successful build");
#ifdef _WIN32
                CloseHandle(g_host_proc_handle);
                g_host_proc_handle = INVALID_HANDLE_VALUE;
#else
                g_host_pid = 0;
#endif
            }
        }

        bool any_changed = false;
        for (auto &w : watchers) {
            if (w.check_for_changes()) any_changed = true;
        }

        if (!any_changed) continue;

        // ---------------------------------------------------------------
        // Source changed — rebuild
        // ---------------------------------------------------------------

        cforge::logger::print_blank();
        cforge::logger::print_action("Reloading", project_name + " module");
        cforge::logger::building(project_name + " (shared library)");

        auto build_start = std::chrono::steady_clock::now();
        bool ok = run_hot_build(project_dir, config, verbose);
        auto build_end = std::chrono::steady_clock::now();
        auto build_ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
            build_end - build_start);

        if (ok) {
            // Write signal file atomically
            ++signal_counter;
            if (!write_signal_file(signal_path, signal_counter)) {
                cforge::logger::print_error(
                    "Signal file is unwritable — aborting hot reload session");
                break;
            }

            cforge::logger::print_action(
                "Reloaded",
                project_name + " module (version " +
                std::to_string(signal_counter + 1) + ") [" +
                std::to_string(build_ms.count()) + "ms]");

            // Relaunch host if it died during a previous failed build
            if (!host_is_alive()) {
                if (!host_exe.empty() && fs::exists(host_exe)) {
                    cforge::logger::running(host_exe.filename().string());
                    launch_host(host_exe.string(), project_dir.string());
                }
            }
        } else {
            cforge::logger::print_warning(
                "Build failed — kept old " + project_name + " module loaded");
            cforge::logger::print_warning("Signal file NOT updated");
        }

        cforge::logger::print_blank();
    }

    // -----------------------------------------------------------------------
    // Shutdown: terminate host, delete signal file
    // -----------------------------------------------------------------------

    cforge::logger::print_blank();
    cforge::logger::print_status("Hot reload session stopped");

    terminate_host();
    delete_signal_file(signal_path);

    return 0;
}
