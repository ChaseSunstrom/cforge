/**
 * @file constants.h
 * @brief Core constants for the CForge library
 */

#ifndef CORE_CONSTANTS_H
#define CORE_CONSTANTS_H

/* Temporary while still in beta */
#ifdef CFORGE_VERSION
#undef CFORGE_VERSION
#endif
#define CFORGE_VERSION "beta-" PROJECT_VERSION

#define CFORGE_REPO_URL "https://github.com/ChaseSunstrom/cforge.git"
#define INDEX_REPO_URL "https://github.com/ChaseSunstrom/cforge-index.git"
#define CFORGE_FILE "cforge.toml"
#define DEFAULT_BUILD_DIR "build"
#define DEFAULT_BIN_DIR "bin"
#define DEFAULT_LIB_DIR "lib"
#define DEFAULT_OBJ_DIR "obj"
#define VCPKG_DEFAULT_DIR "~/.vcpkg"
#define CMAKE_MIN_VERSION "3.15"
#define WORKSPACE_FILE "cforge.workspace.toml"

#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif

#endif