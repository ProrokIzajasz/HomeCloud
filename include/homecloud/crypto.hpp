#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace homecloud::crypto {

[[nodiscard]] std::vector<std::uint8_t> random_bytes(std::size_t count);
[[nodiscard]] std::vector<std::uint8_t> pbkdf2_sha256(
    std::string_view password,
    std::span<const std::uint8_t> salt,
    std::uint32_t iterations,
    std::size_t output_size);
[[nodiscard]] bool constant_time_equal(std::span<const std::uint8_t> left,
                                       std::span<const std::uint8_t> right) noexcept;
[[nodiscard]] std::string hex_encode(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::vector<std::uint8_t> hex_decode(std::string_view text);
[[nodiscard]] std::vector<std::uint8_t> sha256_file(const std::filesystem::path& path);

} // namespace homecloud::crypto
