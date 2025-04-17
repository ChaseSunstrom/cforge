/**
 * @file toml_reader.cpp
 * @brief Implementation of TOML file parsing utilities
 */

#include "core/toml_reader.hpp"
#include "cforge/log.hpp"

#include <fstream>
#include <sstream>
#include <filesystem>

// Include tomlplusplus header
#define TOML_EXCEPTIONS 1
#include <toml++/toml.hpp>

namespace cforge {

// Add constructor to initialize toml_data to nullptr
toml_reader::toml_reader() : toml_data(nullptr) {}

// Add constructor that takes a toml::table directly
toml_reader::toml_reader(const toml::table& table) {
    toml_data = new toml::table(table);
}

// Add destructor to clean up allocated memory
toml_reader::~toml_reader() {
    if (toml_data) {
        delete static_cast<toml::table*>(toml_data);
        toml_data = nullptr;
    }
}

bool toml_reader::load(const std::string& filepath) {
    try {
        // If we have existing data, delete it
        if (toml_data) {
            delete static_cast<toml::table*>(toml_data);
            toml_data = nullptr;
        }
        
        // Check if file exists
        if (!std::filesystem::exists(filepath)) {
            logger::print_error("TOML file does not exist: " + filepath);
            return false;
        }
        
        // Parse the file
        toml_data = new toml::table(toml::parse_file(filepath));
        return true;
    } catch (const toml::parse_error& err) {
        std::stringstream ss;
        ss << "Error parsing TOML file " << filepath << ": " << err.description()
           << " at line " << err.source().begin.line;
        logger::print_error(ss.str());
        return false;
    } catch (const std::exception& ex) {
        logger::print_error("Error reading TOML file " + filepath + ": " + ex.what());
        return false;
    }
}

std::string toml_reader::get_string(const std::string& key, const std::string& default_value) const {
    if (!toml_data) {
        return default_value;
    }
    
    try {
        auto& table = *static_cast<toml::table*>(toml_data);
        auto value = table.at_path(key);
        if (!value || !value.is_string()) {
            return default_value;
        }
        return value.as_string()->get();
    } catch (...) {
        return default_value;
    }
}

int64_t toml_reader::get_int(const std::string& key, int64_t default_value) const {
    if (!toml_data) {
        return default_value;
    }
    
    try {
        auto& table = *static_cast<toml::table*>(toml_data);
        auto value = table.at_path(key);
        if (!value || !value.is_integer()) {
            return default_value;
        }
        return value.as_integer()->get();
    } catch (...) {
        return default_value;
    }
}

bool toml_reader::get_bool(const std::string& key, bool default_value) const {
    if (!toml_data) {
        return default_value;
    }
    
    try {
        auto& table = *static_cast<toml::table*>(toml_data);
        auto value = table.at_path(key);
        if (!value || !value.is_boolean()) {
            return default_value;
        }
        return value.as_boolean()->get();
    } catch (...) {
        return default_value;
    }
}

std::vector<std::string> toml_reader::get_string_array(const std::string& key) const {
    std::vector<std::string> result;
    if (!toml_data) {
        return result;
    }
    
    try {
        auto& table = *static_cast<toml::table*>(toml_data);
        auto value = table.at_path(key);
        if (!value || !value.is_array()) {
            return result;
        }
        
        auto array = value.as_array();
        for (const auto& val : *array) {
            if (val.is_string()) {
                result.push_back(val.as_string()->get());
            }
        }
        return result;
    } catch (...) {
        return result;
    }
}

bool toml_reader::has_key(const std::string& key) const {
    if (!toml_data) {
        return false;
    }
    
    try {
        auto& table = *static_cast<toml::table*>(toml_data);
        auto node = table.at_path(key);
        return static_cast<bool>(node); // Check if node exists
    } catch (...) {
        return false;
    }
}

std::vector<std::string> toml_reader::get_table_keys(const std::string& table_name) const {
    std::vector<std::string> result;
    if (!toml_data) {
        return result;
    }
    
    try {
        auto& root_table = *static_cast<toml::table*>(toml_data);
        const toml::table* table = &root_table;
        
        // Navigate to subtable if specified
        if (!table_name.empty()) {
            auto node = root_table.at_path(table_name);
            if (!node || !node.is_table()) {
                return result;
            }
            table = node.as_table();
        }
        
        // Get all keys in the table
        for (const auto& [key, _] : *table) {
            result.push_back(std::string(key.str()));
        }
        
        return result;
    } catch (...) {
        return result;
    }
}

std::vector<std::string> toml_reader::get_tables(const std::string& prefix) const {
    std::vector<std::string> result;
    if (!toml_data) {
        return result;
    }
    
    try {
        auto& table = *static_cast<toml::table*>(toml_data);
        for (const auto& [key, value] : table) {
            if (value.is_table()) {
                std::string table_name = std::string(key.str());
                if (prefix.empty() || table_name.find(prefix) == 0) {
                    result.push_back(table_name);
                }
            }
        }
        return result;
    } catch (...) {
        return result;
    }
}

} // namespace cforge 