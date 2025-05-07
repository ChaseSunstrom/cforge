#include "core/dependency_hash.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <filesystem>
#include <vector>
#include <iostream>


namespace cforge {

bool dependency_hash::load(const std::filesystem::path& project_dir) {
    std::filesystem::path hash_file = project_dir / HASH_FILE;
    if (!std::filesystem::exists(hash_file)) {
        return false;
    }

    std::ifstream file(hash_file);
    if (!file.is_open()) {
        return false;
    }

    hashes.clear();
    std::string line;
    while (std::getline(file, line)) {
        auto sep = line.find('=');
        if (sep != std::string::npos) {
            std::string name = line.substr(0, sep);
            std::string hash = line.substr(sep + 1);
            hashes[name] = hash;
        }
    }

    return true;
}

bool dependency_hash::save(const std::filesystem::path& project_dir) const {
    std::filesystem::path hash_file = project_dir / HASH_FILE;
    std::ofstream file(hash_file);
    if (!file.is_open()) {
        return false;
    }

    for (const auto& [name, hash] : hashes) {
        file << name << "=" << hash << "\n";
    }

    return true;
}

std::string dependency_hash::get_hash(const std::string& name) const {
    auto it = hashes.find(name);
    return it != hashes.end() ? it->second : "";
}

void dependency_hash::set_hash(const std::string& name, const std::string& hash) {
    hashes[name] = hash;
}

uint64_t dependency_hash::fnv1a_hash(const std::string& str) {
    return fnv1a_hash(str.data(), str.size());
}

uint64_t dependency_hash::fnv1a_hash(const void* data, size_t size) {
    uint64_t hash = FNV_OFFSET_BASIS;
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= FNV_PRIME;
    }
    
    return hash;
}

std::string dependency_hash::hash_to_string(uint64_t hash) {
    std::stringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << hash;
    return ss.str();
}

std::string dependency_hash::calculate_directory_hash(const std::filesystem::path& dir_path) {
    if (!std::filesystem::exists(dir_path) || !std::filesystem::is_directory(dir_path)) {
        return "";
    }

    // Sort entries for consistent hashing
    std::vector<std::filesystem::path> entries;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir_path)) {
        entries.push_back(entry.path());
    }
    std::sort(entries.begin(), entries.end());

    // Calculate hash for each file
    uint64_t combined_hash = FNV_OFFSET_BASIS;
    for (const auto& entry : entries) {
        if (std::filesystem::is_regular_file(entry)) {
            // Add file path relative to dir_path
            std::string rel_path = std::filesystem::relative(entry, dir_path).string();
            combined_hash ^= fnv1a_hash(rel_path);

            // Add file contents
            std::ifstream file(entry, std::ios::binary);
            if (file) {
                char buffer[4096];
                while (file.read(buffer, sizeof(buffer))) {
                    combined_hash ^= fnv1a_hash(buffer, file.gcount());
                }
                if (file.gcount() > 0) {
                    combined_hash ^= fnv1a_hash(buffer, file.gcount());
                }
            }
        }
    }

    return hash_to_string(combined_hash);
}

} // namespace cforge 