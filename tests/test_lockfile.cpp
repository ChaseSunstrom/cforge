/**
 * @file test_lockfile.cpp
 * @brief Integration tests for the lock file mechanism
 */

#include "test_framework.h"
#include "core/lockfile.hpp"
#include "cforge/log.cpp" // prevent errors

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace cforge;

// Helper to create a temporary test directory
static fs::path create_temp_dir() {
    fs::path temp = fs::temp_directory_path() / ("cforge_test_" + std::to_string(std::rand()));
    fs::create_directories(temp);
    return temp;
}

// Helper to clean up test directory
static void cleanup_temp_dir(const fs::path& dir) {
    if (fs::exists(dir)) {
        fs::remove_all(dir);
    }
}

// Helper to write a test lock file
static void write_test_lockfile(const fs::path& dir, const std::string& content) {
    std::ofstream file(dir / "cforge.lock");
    file << content;
    file.close();
}

// Test: Lock file class correctly loads empty/missing file
TEST(Lockfile, LoadMissing) {
    fs::path temp = create_temp_dir();

    lockfile lock;
    bool loaded = lock.load(temp);

    cleanup_temp_dir(temp);

    test_assert(!loaded); // Should return false for missing file
    return 0;
}

// Test: Lock file class correctly loads valid file
TEST(Lockfile, LoadValid) {
    fs::path temp = create_temp_dir();

    const char* content = R"(
# cforge.lock - test file

[metadata]
version = "1"
generated = "2024-01-15T10:30:00Z"

[dependency.fmt]
source = "git"
url = "https://github.com/fmtlib/fmt.git"
version = "11.1.4"
resolved = "abc123def456"

[dependency.spdlog]
source = "vcpkg"
version = "spdlog"
resolved = "spdlog"
)";

    write_test_lockfile(temp, content);

    lockfile lock;
    bool loaded = lock.load(temp);

    test_assert(loaded);
    test_assert(lock.has_dependency("fmt"));
    test_assert(lock.has_dependency("spdlog"));
    test_assert(!lock.has_dependency("nonexistent"));

    auto fmt_dep = lock.get_dependency("fmt");
    test_assert(fmt_dep.has_value());
    test_assert(fmt_dep->name == "fmt");
    test_assert(fmt_dep->source_type == "git");
    test_assert(fmt_dep->url == "https://github.com/fmtlib/fmt.git");
    test_assert(fmt_dep->version == "11.1.4");
    test_assert(fmt_dep->resolved == "abc123def456");

    auto spdlog_dep = lock.get_dependency("spdlog");
    test_assert(spdlog_dep.has_value());
    test_assert(spdlog_dep->source_type == "vcpkg");

    cleanup_temp_dir(temp);
    return 0;
}

// Test: Lock file saves and reloads correctly
TEST(Lockfile, SaveAndReload) {
    fs::path temp = create_temp_dir();

    // Create and save a lock file
    lockfile lock1;
    lock1.lock_vcpkg_dependency("boost", "1.83.0");

    locked_dependency git_dep;
    git_dep.name = "tomlplusplus";
    git_dep.source_type = "git";
    git_dep.url = "https://github.com/marzer/tomlplusplus.git";
    git_dep.version = "v3.4.0";
    git_dep.resolved = "abcdef123456";

    // Use internal save (manual construction)
    bool saved = lock1.save(temp);
    test_assert(saved);

    // Reload and verify
    lockfile lock2;
    bool loaded = lock2.load(temp);
    test_assert(loaded);

    test_assert(lock2.has_dependency("boost"));
    auto boost_dep = lock2.get_dependency("boost");
    test_assert(boost_dep.has_value());
    test_assert(boost_dep->source_type == "vcpkg");
    test_assert(boost_dep->version == "1.83.0");

    cleanup_temp_dir(temp);
    return 0;
}

// Test: Lock file correctly handles removing dependencies
TEST(Lockfile, RemoveDependency) {
    fs::path temp = create_temp_dir();

    lockfile lock;
    lock.lock_vcpkg_dependency("dep1", "1.0.0");
    lock.lock_vcpkg_dependency("dep2", "2.0.0");
    lock.lock_vcpkg_dependency("dep3", "3.0.0");

    test_assert(lock.has_dependency("dep1"));
    test_assert(lock.has_dependency("dep2"));
    test_assert(lock.has_dependency("dep3"));

    lock.remove_dependency("dep2");

    test_assert(lock.has_dependency("dep1"));
    test_assert(!lock.has_dependency("dep2"));
    test_assert(lock.has_dependency("dep3"));

    cleanup_temp_dir(temp);
    return 0;
}

// Test: Lock file clear removes all dependencies
TEST(Lockfile, Clear) {
    fs::path temp = create_temp_dir();

    lockfile lock;
    lock.lock_vcpkg_dependency("dep1", "1.0.0");
    lock.lock_vcpkg_dependency("dep2", "2.0.0");

    test_assert(lock.get_all().size() == 2);

    lock.clear();

    test_assert(lock.get_all().empty());
    test_assert(!lock.has_dependency("dep1"));
    test_assert(!lock.has_dependency("dep2"));

    cleanup_temp_dir(temp);
    return 0;
}

// Test: Lock file exists check
TEST(Lockfile, Exists) {
    fs::path temp = create_temp_dir();

    test_assert(!lockfile::exists(temp));

    write_test_lockfile(temp, "# test");

    test_assert(lockfile::exists(temp));

    cleanup_temp_dir(temp);
    return 0;
}

// Test: Lock file handles quoted values correctly
TEST(Lockfile, QuotedValues) {
    fs::path temp = create_temp_dir();

    const char* content = R"(
[dependency.test]
source = "git"
url = "https://example.com/repo.git"
version = "v1.0.0"
resolved = "deadbeef"
checksum = "sha256:abc123"
)";

    write_test_lockfile(temp, content);

    lockfile lock;
    bool loaded = lock.load(temp);
    test_assert(loaded);

    auto dep = lock.get_dependency("test");
    test_assert(dep.has_value());
    test_assert(dep->url == "https://example.com/repo.git");
    test_assert(dep->checksum == "sha256:abc123");

    cleanup_temp_dir(temp);
    return 0;
}

// Test: Lock file skips comments and empty lines
TEST(Lockfile, SkipsComments) {
    fs::path temp = create_temp_dir();

    const char* content = R"(
# This is a comment

# Another comment
[dependency.mylib]
source = "git"
# inline section comment
url = "https://github.com/test/mylib.git"

version = "1.0.0"
resolved = "abc123"
)";

    write_test_lockfile(temp, content);

    lockfile lock;
    bool loaded = lock.load(temp);
    test_assert(loaded);

    test_assert(lock.has_dependency("mylib"));
    auto dep = lock.get_dependency("mylib");
    test_assert(dep.has_value());
    test_assert(dep->version == "1.0.0");

    cleanup_temp_dir(temp);
    return 0;
}

// Test: Multiple dependencies in sequence
TEST(Lockfile, MultipleDependencies) {
    fs::path temp = create_temp_dir();

    const char* content = R"(
[dependency.first]
source = "git"
url = "https://github.com/test/first.git"
version = "1.0.0"
resolved = "aaa111"

[dependency.second]
source = "vcpkg"
version = "2.0.0"
resolved = "2.0.0"

[dependency.third]
source = "git"
url = "https://github.com/test/third.git"
version = "3.0.0"
resolved = "ccc333"
)";

    write_test_lockfile(temp, content);

    lockfile lock;
    bool loaded = lock.load(temp);
    test_assert(loaded);

    test_assert(lock.get_all().size() == 3);

    test_assert(lock.has_dependency("first"));
    test_assert(lock.has_dependency("second"));
    test_assert(lock.has_dependency("third"));

    auto first = lock.get_dependency("first");
    test_assert(first->resolved == "aaa111");

    auto second = lock.get_dependency("second");
    test_assert(second->source_type == "vcpkg");

    auto third = lock.get_dependency("third");
    test_assert(third->resolved == "ccc333");

    cleanup_temp_dir(temp);
    return 0;
}

// Test: Get nonexistent dependency returns nullopt
TEST(Lockfile, GetNonexistent) {
    lockfile lock;
    auto dep = lock.get_dependency("doesnotexist");
    test_assert(!dep.has_value());
    return 0;
}

// Test: Lock vcpkg dependency with triplet suffix
TEST(Lockfile, VcpkgWithTriplet) {
    lockfile lock;
    lock.lock_vcpkg_dependency("fmt", "fmt:x64-windows");

    auto dep = lock.get_dependency("fmt");
    test_assert(dep.has_value());
    test_assert(dep->name == "fmt");
    test_assert(dep->source_type == "vcpkg");
    test_assert(dep->version == "fmt:x64-windows");

    return 0;
}
