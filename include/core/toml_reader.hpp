/**
 * @file toml_reader.hpp
 * @brief TOML file parsing utilities using tomlplusplus
 */

#ifndef CFORGE_TOML_READER_H
#define CFORGE_TOML_READER_H

#include <string>
#include <string_view>
#include <vector>
#include <optional>

#include "core/constants.h"
#include "core/types.h"

namespace cforge {

/**
 * @brief Class for reading and parsing TOML configuration files
 */
class toml_reader {
public:
    /**
     * @brief Constructor
     */
    toml_reader();
    
    /**
     * @brief Destructor
     */
    ~toml_reader();
    
    /**
     * @brief Load and parse a TOML file
     * @param filepath Path to the TOML file
     * @return True if the file was successfully loaded and parsed
     */
    bool load(const std::string& filepath);
    
    /**
     * @brief Get a string value from the TOML file
     * @param key The key to look up (can be dotted for tables)
     * @param default_value The default value to return if the key is not found
     * @return The value associated with the key, or default_value if not found
     */
    std::string get_string(const std::string& key, const std::string& default_value = "") const;
    
    /**
     * @brief Get an integer value from the TOML file
     * @param key The key to look up (can be dotted for tables)
     * @param default_value The default value to return if the key is not found
     * @return The value associated with the key, or default_value if not found
     */
    int64_t get_int(const std::string& key, int64_t default_value = 0) const;
    
    /**
     * @brief Get a boolean value from the TOML file
     * @param key The key to look up (can be dotted for tables)
     * @param default_value The default value to return if the key is not found
     * @return The value associated with the key, or default_value if not found
     */
    bool get_bool(const std::string& key, bool default_value = false) const;
    
    /**
     * @brief Get a string array from the TOML file
     * @param key The key to look up (can be dotted for tables)
     * @return The array associated with the key, or empty vector if not found
     */
    std::vector<std::string> get_string_array(const std::string& key) const;
    
    /**
     * @brief Check if a key exists in the TOML file
     * @param key The key to look up (can be dotted for tables)
     * @return True if the key exists
     */
    bool has_key(const std::string& key) const;
    
    /**
     * @brief Get all keys in a table
     * @param table The table name (empty for root table)
     * @return Vector of keys in the table
     */
    std::vector<std::string> get_table_keys(const std::string& table = "") const;
    
    /**
     * @brief Get all tables that match a prefix
     * @param prefix The prefix to match
     * @return Vector of table names
     */
    std::vector<std::string> get_tables(const std::string& prefix = "") const;

private:
    cforge_pointer_t toml_data; // Opaque pointer to the toml::table
};

} // namespace cforge

#endif // CFORGE_TOML_READER_H 