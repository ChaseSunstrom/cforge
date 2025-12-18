/**
 * @file toml_reader.hpp
 * @brief TOML file parsing utilities using tomlplusplus
 */

#ifndef CFORGE_TOML_READER_H
#define CFORGE_TOML_READER_H

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/constants.h"
#include "core/types.h"

#include <toml++/toml.hpp>

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
   * @brief Constructor that takes a toml::table directly
   * @param table The TOML table to wrap
   */
  explicit toml_reader(const toml::table &table);

  /**
   * @brief Destructor
   */
  ~toml_reader();

  /**
   * @brief Load and parse a TOML file
   * @param filepath Path to the TOML file
   * @return True if the file was successfully loaded and parsed
   */
  bool load(const std::string &filepath);

  /**
   * @brief Get a string value from the TOML file
   * @param key The key to look up (can be dotted for tables)
   * @param default_value The default value to return if the key is not found
   * @return The value associated with the key, or default_value if not found
   */
  std::string get_string(const std::string &key,
                         const std::string &default_value = "") const;

  /**
   * @brief Get an integer value from the TOML file
   * @param key The key to look up (can be dotted for tables)
   * @param default_value The default value to return if the key is not found
   * @return The value associated with the key, or default_value if not found
   */
  int64_t get_int(const std::string &key, int64_t default_value = 0) const;

  /**
   * @brief Get a boolean value from the TOML file
   * @param key The key to look up (can be dotted for tables)
   * @param default_value The default value to return if the key is not found
   * @return The value associated with the key, or default_value if not found
   */
  bool get_bool(const std::string &key, bool default_value = false) const;

  /**
   * @brief Get a string array from the TOML file
   * @param key The key to look up (can be dotted for tables)
   * @return The array associated with the key, or empty vector if not found
   */
  std::vector<std::string> get_string_array(const std::string &key) const;

  /**
   * @brief Check if a key exists in the TOML file
   * @param key The key to look up (can be dotted for tables)
   * @return True if the key exists
   */
  bool has_key(const std::string &key) const;

  /**
   * @brief Get all keys in a table
   * @param table The table name (empty for root table)
   * @return Vector of keys in the table
   */
  std::vector<std::string> get_table_keys(const std::string &table = "") const;

  /**
   * @brief Get all tables that match a prefix
   * @param prefix The prefix to match
   * @return Vector of table names
   */
  std::vector<std::string> get_tables(const std::string &prefix = "") const;

  /**
   * @brief Get a string map (inline table) from the TOML file
   * @param key The key to look up (can be dotted for tables)
   * @return Map of string key-value pairs, or empty map if not found
   */
  std::map<std::string, std::string> get_string_map(const std::string &key) const;

  /**
   * @brief Get an array of tables from the TOML file
   * @param key The key to look up (e.g., "test.targets" for [[test.targets]])
   * @return Vector of toml_reader objects, each wrapping one table from the array
   */
  std::vector<toml_reader> get_table_array(const std::string &key) const;

  /**
   * @brief Get a sub-table as a new toml_reader
   * @param key The table key to look up (can be dotted)
   * @return Optional toml_reader wrapping the sub-table, or empty if not found
   */
  std::optional<toml_reader> get_table(const std::string &key) const;

  /**
   * @brief Get a string value with fallback to deprecated key
   * @param key The preferred key to look up
   * @param deprecated_key The old deprecated key to try if preferred not found
   * @param default_value The default value if neither key exists
   * @param warn If true, emit deprecation warning when using deprecated key
   * @return The value found, or default_value if neither key exists
   */
  std::string get_string_or_deprecated(const std::string &key,
                                       const std::string &deprecated_key,
                                       const std::string &default_value = "",
                                       bool warn = true) const;

  /**
   * @brief Check if either a key or its deprecated version exists
   * @param key The preferred key
   * @param deprecated_key The deprecated key to check as fallback
   * @return True if either key exists
   */
  bool has_key_or_deprecated(const std::string &key,
                             const std::string &deprecated_key) const;

  /**
   * @brief Get the underlying toml::table pointer (for advanced usage)
   * @return Pointer to the toml::table, or nullptr if not loaded
   */
  const void* get_table() const { return toml_data; }

private:
  cforge_pointer_t toml_data; // Opaque pointer to the toml::table
};

} // namespace cforge

#endif // CFORGE_TOML_READER_H