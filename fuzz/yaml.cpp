#include "yaml.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>

extern "C" int LLVMFuzzerInitialize(int*, char***) {
    // rapidyaml explicitly supports throwing from these C++ error callbacks.
    // Keep malformed-input diagnostics from flooding the fuzz runner's stderr.
    auto callbacks = ryml::get_callbacks();
    callbacks.set_error_basic([](c4::csubstr, const ryml::ErrorDataBasic&, void*) {
        throw std::runtime_error("invalid YAML");
    });
    callbacks.set_error_parse([](c4::csubstr, const ryml::ErrorDataParse&, void*) {
        throw std::runtime_error("invalid YAML");
    });
    callbacks.set_error_visit([](c4::csubstr, const ryml::ErrorDataVisit&, void*) {
        throw std::runtime_error("invalid YAML");
    });
    ryml::set_callbacks(callbacks);
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    std::string_view input(reinterpret_cast<const char*>(data), size);
    (void)slimr::parse_yaml(input);
    (void)slimr::parse_frontmatter(input);
    return 0;
}
