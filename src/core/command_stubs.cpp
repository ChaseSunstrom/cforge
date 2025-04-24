#include "cforge/log.hpp"
#include "core/commands.hpp"
#include <string>

using namespace cforge;

/**
 * @brief Wrapper for the help command (when needed)
 */
extern "C" cforge_int_t cforge_cmd_help_wrapper(cforge_context_t *ctx) {
  return cforge_cmd_help(ctx);
}