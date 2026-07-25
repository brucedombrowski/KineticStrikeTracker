#include "kst/version.hpp"

#include <string>

namespace kst {

const char* version_string() {
    static const std::string s = std::string("KineticStrikeTracker ") + kVersion;
    return s.c_str();
}

}  // namespace kst
