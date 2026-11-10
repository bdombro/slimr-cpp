# slimr::encoding (`packages/encoding`)

Fast, clean C++26 binary encoding and decoding library for Hex, Base64, and Base64URL (RFC 4648), featuring `std::expected` error handling.

Borrowed directly from the design of [`@slimr/encoding`](https://www.npmjs.com/package/@slimr/encoding).

## Why `slimr::encoding`?

- **Zero-allocation Overhead:** Optimized buffer reservation for fast string encoding and decoding.
- **Robust `std::expected` Validation:** Decoders catch malformed hex digits or invalid base64 characters cleanly without exceptions.
- **URL-safe Base64URL:** Native support for RFC 4648 Base64URL encoding (`-` and `_`) with optional padding.

---

## Quick Start

```cpp
#include <slimr/encoding.hpp>
#include <iostream>

using namespace slimr;

int main() {
    std::string data = "Hello Slimr Encoding!";

    // Hex encode / decode
    std::string hex = hex_encode(data);
    auto decodedHex = hex_decode(hex);
    if (decodedHex) {
        std::cout << "Decoded Hex: " << *decodedHex << "\n";
    }

    // Base64 encode / decode
    std::string b64 = base64_encode(data);
    auto decodedB64 = base64_decode(b64);
    if (decodedB64) {
        std::cout << "Decoded Base64: " << *decodedB64 << "\n";
    }

    // Base64URL encode / decode (no padding by default)
    std::string b64url = base64url_encode(data, false);
    auto decodedB64url = base64url_decode(b64url);

    return 0;
}
```

---

## API Cheat Sheet

| Function | Signature | Description |
| --- | --- | --- |
| `hex_encode` | `inline std::string hex_encode(std::string_view in, bool uppercase = false)` | Encodes binary data to lowercase or uppercase hexadecimal string. |
| `hex_decode` | `inline std::expected<std::string, std::string> hex_decode(std::string_view in)` | Decodes hex string into binary string within `std::expected`. |
| `base64_encode` | `inline std::string base64_encode(std::string_view in)` | Encodes binary data to standard RFC 4648 Base64 string with padding. |
| `base64url_encode` | `inline std::string base64url_encode(std::string_view in, bool pad = false)` | Encodes binary data to URL-safe Base64URL string (`-` and `_`). |
| `base64_decode` | `inline std::expected<std::string, std::string> base64_decode(std::string_view in)` | Decodes standard or URL-safe Base64 string within `std::expected`. |
| `base64url_decode` | `inline std::expected<std::string, std::string> base64url_decode(std::string_view in)` | Convenience alias for `base64_decode`. |
