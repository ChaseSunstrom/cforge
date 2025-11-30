/**
 * @file command_new.cpp
 * @brief Implementation of the new command for creating files from templates
 */

#include "cforge/log.hpp"
#include "core/commands.hpp"
#include "core/toml_reader.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

/**
 * @brief Convert string to PascalCase
 */
std::string to_pascal_case(const std::string &input) {
  std::string result;
  bool capitalize_next = true;

  for (char c : input) {
    if (c == '_' || c == '-' || c == ' ') {
      capitalize_next = true;
    } else if (capitalize_next) {
      result += std::toupper(c);
      capitalize_next = false;
    } else {
      result += c;
    }
  }
  return result;
}

/**
 * @brief Convert string to snake_case
 */
std::string to_snake_case(const std::string &input) {
  std::string result;
  for (size_t i = 0; i < input.length(); i++) {
    char c = input[i];
    if (std::isupper(c)) {
      if (i > 0 && !std::isupper(input[i - 1])) {
        result += '_';
      }
      result += std::tolower(c);
    } else if (c == '-' || c == ' ') {
      result += '_';
    } else {
      result += c;
    }
  }
  return result;
}

/**
 * @brief Convert string to UPPER_CASE
 */
std::string to_upper_case(const std::string &input) {
  std::string snake = to_snake_case(input);
  std::transform(snake.begin(), snake.end(), snake.begin(), ::toupper);
  return snake;
}

/**
 * @brief Generate a C++ class header file
 */
bool generate_class_header(const fs::path &path, const std::string &class_name,
                           const std::string &namespace_name) {
  std::ofstream file(path);
  if (!file)
    return false;

  std::string guard = to_upper_case(class_name) + "_HPP";
  std::string pascal_name = to_pascal_case(class_name);

  file << "#pragma once\n\n";
  file << "#ifndef " << guard << "\n";
  file << "#define " << guard << "\n\n";

  if (!namespace_name.empty()) {
    file << "namespace " << namespace_name << " {\n\n";
  }

  file << "/**\n";
  file << " * @brief " << pascal_name << " class\n";
  file << " */\n";
  file << "class " << pascal_name << " {\n";
  file << "public:\n";
  file << "    /**\n";
  file << "     * @brief Default constructor\n";
  file << "     */\n";
  file << "    " << pascal_name << "() = default;\n\n";
  file << "    /**\n";
  file << "     * @brief Destructor\n";
  file << "     */\n";
  file << "    ~" << pascal_name << "() = default;\n\n";
  file << "    // Copy operations\n";
  file << "    " << pascal_name << "(const " << pascal_name
       << "&) = default;\n";
  file << "    " << pascal_name << "& operator=(const " << pascal_name
       << "&) = default;\n\n";
  file << "    // Move operations\n";
  file << "    " << pascal_name << "(" << pascal_name
       << "&&) noexcept = default;\n";
  file << "    " << pascal_name << "& operator=(" << pascal_name
       << "&&) noexcept = default;\n\n";
  file << "private:\n";
  file << "    // Member variables\n";
  file << "};\n";

  if (!namespace_name.empty()) {
    file << "\n} // namespace " << namespace_name << "\n";
  }

  file << "\n#endif // " << guard << "\n";

  return true;
}

/**
 * @brief Generate a C++ class implementation file
 */
bool generate_class_source(const fs::path &path, const std::string &class_name,
                           const std::string &header_name,
                           const std::string &namespace_name) {
  std::ofstream file(path);
  if (!file)
    return false;

  std::string pascal_name = to_pascal_case(class_name);

  file << "#include \"" << header_name << "\"\n\n";

  if (!namespace_name.empty()) {
    file << "namespace " << namespace_name << " {\n\n";
  }

  file << "// " << pascal_name << " implementation\n\n";

  if (!namespace_name.empty()) {
    file << "} // namespace " << namespace_name << "\n";
  }

  return true;
}

/**
 * @brief Generate a header-only file
 */
bool generate_header(const fs::path &path, const std::string &name,
                     const std::string &namespace_name) {
  std::ofstream file(path);
  if (!file)
    return false;

  std::string guard = to_upper_case(name) + "_HPP";

  file << "#pragma once\n\n";
  file << "#ifndef " << guard << "\n";
  file << "#define " << guard << "\n\n";

  if (!namespace_name.empty()) {
    file << "namespace " << namespace_name << " {\n\n";
  }

  file << "// TODO: Add declarations here\n";

  if (!namespace_name.empty()) {
    file << "\n} // namespace " << namespace_name << "\n";
  }

  file << "\n#endif // " << guard << "\n";

  return true;
}

/**
 * @brief Generate a test file
 */
bool generate_test(const fs::path &path, const std::string &test_name,
                   const std::string &test_framework) {
  std::ofstream file(path);
  if (!file)
    return false;

  std::string pascal_name = to_pascal_case(test_name);

  if (test_framework == "catch2") {
    file << "#include <catch2/catch_test_macros.hpp>\n\n";
    file << "TEST_CASE(\"" << pascal_name << " tests\", \"[" << test_name
         << "]\") {\n";
    file << "    SECTION(\"basic test\") {\n";
    file << "        REQUIRE(true);\n";
    file << "    }\n";
    file << "}\n";
  } else if (test_framework == "gtest" || test_framework == "googletest") {
    file << "#include <gtest/gtest.h>\n\n";
    file << "class " << pascal_name << "Test : public ::testing::Test {\n";
    file << "protected:\n";
    file << "    void SetUp() override {\n";
    file << "        // Setup code here\n";
    file << "    }\n\n";
    file << "    void TearDown() override {\n";
    file << "        // Teardown code here\n";
    file << "    }\n";
    file << "};\n\n";
    file << "TEST_F(" << pascal_name << "Test, BasicTest) {\n";
    file << "    EXPECT_TRUE(true);\n";
    file << "}\n";
  } else {
    // Basic test without framework
    file << "#include <cassert>\n";
    file << "#include <iostream>\n\n";
    file << "void test_" << to_snake_case(test_name) << "() {\n";
    file << "    // TODO: Add test code here\n";
    file << "    assert(true);\n";
    file << "    std::cout << \"" << pascal_name
         << " tests passed!\" << std::endl;\n";
    file << "}\n\n";
    file << "int main() {\n";
    file << "    test_" << to_snake_case(test_name) << "();\n";
    file << "    return 0;\n";
    file << "}\n";
  }

  return true;
}

/**
 * @brief Generate a main file
 */
bool generate_main(const fs::path &path) {
  std::ofstream file(path);
  if (!file)
    return false;

  file << "#include <iostream>\n\n";
  file << "int main(int argc, char* argv[]) {\n";
  file << "    std::cout << \"Hello, World!\" << std::endl;\n";
  file << "    return 0;\n";
  file << "}\n";

  return true;
}

/**
 * @brief Generate a struct header
 */
bool generate_struct(const fs::path &path, const std::string &struct_name,
                     const std::string &namespace_name) {
  std::ofstream file(path);
  if (!file)
    return false;

  std::string guard = to_upper_case(struct_name) + "_HPP";
  std::string pascal_name = to_pascal_case(struct_name);

  file << "#pragma once\n\n";
  file << "#ifndef " << guard << "\n";
  file << "#define " << guard << "\n\n";

  if (!namespace_name.empty()) {
    file << "namespace " << namespace_name << " {\n\n";
  }

  file << "/**\n";
  file << " * @brief " << pascal_name << " data structure\n";
  file << " */\n";
  file << "struct " << pascal_name << " {\n";
  file << "    // Member variables\n";
  file << "};\n";

  if (!namespace_name.empty()) {
    file << "\n} // namespace " << namespace_name << "\n";
  }

  file << "\n#endif // " << guard << "\n";

  return true;
}

/**
 * @brief Generate an interface (abstract class) header
 */
bool generate_interface(const fs::path &path, const std::string &interface_name,
                        const std::string &namespace_name) {
  std::ofstream file(path);
  if (!file)
    return false;

  std::string guard = to_upper_case(interface_name) + "_HPP";
  std::string pascal_name = to_pascal_case(interface_name);

  file << "#pragma once\n\n";
  file << "#ifndef " << guard << "\n";
  file << "#define " << guard << "\n\n";

  if (!namespace_name.empty()) {
    file << "namespace " << namespace_name << " {\n\n";
  }

  file << "/**\n";
  file << " * @brief " << pascal_name << " interface\n";
  file << " */\n";
  file << "class " << pascal_name << " {\n";
  file << "public:\n";
  file << "    virtual ~" << pascal_name << "() = default;\n\n";
  file << "    // Pure virtual methods\n";
  file << "    // virtual void method() = 0;\n";
  file << "\n";
  file << "protected:\n";
  file << "    " << pascal_name << "() = default;\n";
  file << "    " << pascal_name << "(const " << pascal_name
       << "&) = default;\n";
  file << "    " << pascal_name << "& operator=(const " << pascal_name
       << "&) = default;\n";
  file << "};\n";

  if (!namespace_name.empty()) {
    file << "\n} // namespace " << namespace_name << "\n";
  }

  file << "\n#endif // " << guard << "\n";

  return true;
}

} // anonymous namespace

/**
 * @brief Handle the 'new' command for creating files from templates
 */
cforge_int_t cforge_cmd_new(const cforge_context_t *ctx) {
  using namespace cforge;

  fs::path project_dir = ctx->working_dir;

  // Parse arguments
  std::string template_type;
  std::string name;
  std::string namespace_name;
  std::string output_dir;
  bool force = false;

  for (int i = 0; i < ctx->args.arg_count; i++) {
    std::string arg = ctx->args.args[i];
    if (arg == "-n" || arg == "--namespace") {
      if (i + 1 < ctx->args.arg_count) {
        namespace_name = ctx->args.args[++i];
      }
    } else if (arg == "-o" || arg == "--output") {
      if (i + 1 < ctx->args.arg_count) {
        output_dir = ctx->args.args[++i];
      }
    } else if (arg == "-f" || arg == "--force") {
      force = true;
    } else if (template_type.empty()) {
      template_type = arg;
    } else if (name.empty()) {
      name = arg;
    }
  }

  if (template_type.empty()) {
    logger::print_plain("cforge new - Create files from templates");
    logger::print_plain("");
    logger::print_plain("Usage: cforge new <template> <name> [options]");
    logger::print_plain("");
    logger::print_plain("Templates:");
    logger::print_plain(
        "  class      Create a class with header and source files");
    logger::print_plain("  header     Create a header-only file");
    logger::print_plain("  struct     Create a struct header file");
    logger::print_plain("  interface  Create an interface (abstract class)");
    logger::print_plain("  test       Create a test file");
    logger::print_plain("  main       Create a main.cpp file");
    logger::print_plain("");
    logger::print_plain("Options:");
    logger::print_plain("  -n, --namespace <name>  Wrap in namespace");
    logger::print_plain("  -o, --output <dir>      Output directory");
    logger::print_plain("  -f, --force             Overwrite existing files");
    logger::print_plain("");
    logger::print_plain("Examples:");
    logger::print_plain("  cforge new class MyClass");
    logger::print_plain("  cforge new class MyClass -n myproject");
    logger::print_plain("  cforge new header utils -o include/myproject");
    logger::print_plain("  cforge new test MyClass --framework catch2");
    return 0;
  }

  if (name.empty() && template_type != "main") {
    logger::print_error("Please specify a name for the " + template_type);
    return 1;
  }

  // Try to get namespace from project config
  if (namespace_name.empty()) {
    fs::path config_file = project_dir / "cforge.toml";
    if (fs::exists(config_file)) {
      toml_reader config;
      config.load(config_file.string());
      namespace_name = config.get_string("project.namespace", "");
    }
  }

  // Determine output directories
  fs::path include_dir = project_dir / "include";
  fs::path src_dir = project_dir / "src";
  fs::path test_dir = project_dir / "tests";

  if (!output_dir.empty()) {
    include_dir = project_dir / output_dir;
    src_dir = project_dir / output_dir;
    test_dir = project_dir / output_dir;
  }

  std::string snake_name = to_snake_case(name);

  if (template_type == "class") {
    // Create both header and source
    fs::path header_path = include_dir / (snake_name + ".hpp");
    fs::path source_path = src_dir / (snake_name + ".cpp");

    // Create directories
    fs::create_directories(header_path.parent_path());
    fs::create_directories(source_path.parent_path());

    // Check if files exist
    if (!force && (fs::exists(header_path) || fs::exists(source_path))) {
      logger::print_error("File(s) already exist. Use --force to overwrite.");
      return 1;
    }

    if (!generate_class_header(header_path, name, namespace_name)) {
      logger::print_error("Failed to create " + header_path.string());
      return 1;
    }
    logger::print_action("Created", header_path.string());

    std::string header_include = snake_name + ".hpp";
    if (!generate_class_source(source_path, name, header_include,
                               namespace_name)) {
      logger::print_error("Failed to create " + source_path.string());
      return 1;
    }
    logger::print_action("Created", source_path.string());

  } else if (template_type == "header") {
    fs::path header_path = include_dir / (snake_name + ".hpp");
    fs::create_directories(header_path.parent_path());

    if (!force && fs::exists(header_path)) {
      logger::print_error("File already exists. Use --force to overwrite.");
      return 1;
    }

    if (!generate_header(header_path, name, namespace_name)) {
      logger::print_error("Failed to create " + header_path.string());
      return 1;
    }
    logger::print_action("Created", header_path.string());

  } else if (template_type == "struct") {
    fs::path header_path = include_dir / (snake_name + ".hpp");
    fs::create_directories(header_path.parent_path());

    if (!force && fs::exists(header_path)) {
      logger::print_error("File already exists. Use --force to overwrite.");
      return 1;
    }

    if (!generate_struct(header_path, name, namespace_name)) {
      logger::print_error("Failed to create " + header_path.string());
      return 1;
    }
    logger::print_action("Created", header_path.string());

  } else if (template_type == "interface") {
    fs::path header_path = include_dir / (snake_name + ".hpp");
    fs::create_directories(header_path.parent_path());

    if (!force && fs::exists(header_path)) {
      logger::print_error("File already exists. Use --force to overwrite.");
      return 1;
    }

    if (!generate_interface(header_path, name, namespace_name)) {
      logger::print_error("Failed to create " + header_path.string());
      return 1;
    }
    logger::print_action("Created", header_path.string());

  } else if (template_type == "test") {
    fs::path test_path = test_dir / ("test_" + snake_name + ".cpp");
    fs::create_directories(test_path.parent_path());

    if (!force && fs::exists(test_path)) {
      logger::print_error("File already exists. Use --force to overwrite.");
      return 1;
    }

    // Try to detect test framework from config
    std::string test_framework = "";
    fs::path config_file = project_dir / "cforge.toml";
    if (fs::exists(config_file)) {
      toml_reader config;
      config.load(config_file.string());
      test_framework = config.get_string("test.framework", "");
    }

    if (!generate_test(test_path, name, test_framework)) {
      logger::print_error("Failed to create " + test_path.string());
      return 1;
    }
    logger::print_action("Created", test_path.string());

  } else if (template_type == "main") {
    fs::path main_path = src_dir / "main.cpp";
    fs::create_directories(main_path.parent_path());

    if (!force && fs::exists(main_path)) {
      logger::print_error("File already exists. Use --force to overwrite.");
      return 1;
    }

    if (!generate_main(main_path)) {
      logger::print_error("Failed to create " + main_path.string());
      return 1;
    }
    logger::print_action("Created", main_path.string());

  } else {
    logger::print_error("Unknown template: " + template_type);
    logger::print_plain(
        "Available templates: class, header, struct, interface, test, main");
    return 1;
  }

  return 0;
}
