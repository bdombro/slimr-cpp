#include "json.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    std::string_view input(reinterpret_cast<const char*>(data), size);
    auto parsed = slimr::parse_json(input);
    if (parsed) {
        auto encoded = slimr::to_json(*parsed);
        (void)slimr::parse_json(encoded);
    }
    return 0;
}
