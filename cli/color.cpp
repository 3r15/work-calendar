#include "cli/color.h"

#include <cstdlib>

namespace cli {

bool noColorRequested() {
    const char* value = std::getenv("NO_COLOR");
    return value != nullptr && value[0] != '\0';
}

}  // namespace cli
