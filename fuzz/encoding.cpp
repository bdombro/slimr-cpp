#include "encoding.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    std::string_view input(reinterpret_cast<const char*>(data), size);
    (void)slimr::hex_decode(input);
    (void)slimr::base64_decode(input);
    (void)slimr::base64url_decode(input);
    auto hex = slimr::hex_decode(slimr::hex_encode(input));
    auto base64 = slimr::base64_decode(slimr::base64_encode(input));
    auto url = slimr::base64url_decode(slimr::base64url_encode(input));
    if (!hex || *hex != input || !base64 || *base64 != input || !url || *url != input) {
        std::abort();
    }
    return 0;
}
