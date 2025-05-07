#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <cstdint>

namespace cforge {

/**
 * @brief Class for tracking dependency states through hashing
 */
class dependency_hash {
public:
    dependency_hash() = default;
    ~dependency_hash() = default;

    /**
     * @brief Load dependency hashes from file
     * @param project_dir Project directory path
     * @return true if successful, false if file doesn't exist or read fails
     */
    bool load(const std::filesystem::path& project_dir);

    /**
     * @brief Save dependency hashes to file
     * @param project_dir Project directory path
     * @return true if successful
     */
    bool save(const std::filesystem::path& project_dir) const;

    /**
     * @brief Get hash for a dependency
     * @param name Dependency name
     * @return Hash string if found, empty string if not found
     */
    std::string get_hash(const std::string& name) const;

    /**
     * @brief Set hash for a dependency
     * @param name Dependency name
     * @param hash Hash value
     */
    void set_hash(const std::string& name, const std::string& hash);

    /**
     * @brief Get version for a dependency
     * @param name Dependency name
     * @return Version string if found, empty string if not found
     */
    std::string get_version(const std::string& name) const;

    /**
     * @brief Set version for a dependency
     * @param name Dependency name
     * @param version Version value (tag, branch, or commit)
     */
    void set_version(const std::string& name, const std::string& version);

    /**
     * @brief Calculate hash for a file's content
     * @param content File content
     * @return Hash string based on file content
     */
    std::string calculate_file_content_hash(const std::string& content) const {
        return hash_to_string(fnv1a_hash(content));
    }

    /**
     * @brief Calculate hash for a directory
     * @param dir_path Directory path
     * @return Hash string based on directory contents
     */
    static std::string calculate_directory_hash(const std::filesystem::path& dir_path);

private:
    std::unordered_map<std::string, std::string> hashes;
    std::unordered_map<std::string, std::string> versions;
    static constexpr const char* HASH_FILE = ".cforge_dependency_hashes";

    // FNV-1a hash constants
    static constexpr uint64_t FNV_PRIME = 1099511628211ULL;
    static constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;

    /**
     * @brief Calculate FNV-1a hash for a string
     * @param str Input string
     * @return 64-bit hash value
     */
    static uint64_t fnv1a_hash(const std::string& str);

    /**
     * @brief Calculate FNV-1a hash for binary data
     * @param data Pointer to data
     * @param size Size of data in bytes
     * @return 64-bit hash value
     */
    static uint64_t fnv1a_hash(const void* data, size_t size);

    /**
     * @brief Convert 64-bit hash to hex string
     * @param hash 64-bit hash value
     * @return Hex string representation
     */
    static std::string hash_to_string(uint64_t hash);
};

} // namespace cforge 