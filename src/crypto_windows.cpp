#include "homecloud/crypto.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

using namespace std;

namespace homecloud::crypto {

std::vector<std::uint8_t> random_bytes(std::size_t count) {
    std::vector<std::uint8_t> result(count);
    if (BCryptGenRandom(nullptr, result.data(), static_cast<ULONG>(result.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
        throw std::runtime_error("Secure random generation failed");
    }
    return result;
}

std::vector<std::uint8_t> pbkdf2_sha256(std::string_view password,
                                        std::span<const std::uint8_t> salt,
                                        std::uint32_t iterations,
                                        std::size_t output_size) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr,
                                    BCRYPT_ALG_HANDLE_HMAC_FLAG) < 0) {
        throw std::runtime_error("Could not initialize PBKDF2");
    }
    std::vector<std::uint8_t> result(output_size);
    const auto status = BCryptDeriveKeyPBKDF2(
        algorithm,
        reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())),
        static_cast<ULONG>(password.size()),
        const_cast<PUCHAR>(salt.data()), static_cast<ULONG>(salt.size()),
        iterations, result.data(), static_cast<ULONG>(result.size()), 0);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) {
        throw std::runtime_error("PBKDF2 password hashing failed");
    }
    return result;
}

bool constant_time_equal(std::span<const std::uint8_t> left,
                         std::span<const std::uint8_t> right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    std::uint8_t difference = 0;
    for (std::size_t index = 0; index < left.size(); ++index) {
        difference |= left[index] ^ right[index];
    }
    return difference == 0;
}

std::string hex_encode(std::span<const std::uint8_t> bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result(bytes.size() * 2, '0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        result[index * 2] = digits[bytes[index] >> 4];
        result[index * 2 + 1] = digits[bytes[index] & 0x0f];
    }
    return result;
}

std::vector<std::uint8_t> hex_decode(std::string_view text) {
    if (text.size() % 2 != 0) {
        throw std::invalid_argument("Invalid hexadecimal value");
    }
    auto nibble = [](char value) -> std::uint8_t {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        throw std::invalid_argument("Invalid hexadecimal value");
    };
    std::vector<std::uint8_t> result(text.size() / 2);
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = static_cast<std::uint8_t>(
            (nibble(text[index * 2]) << 4) | nibble(text[index * 2 + 1]));
    }
    return result;
}

std::vector<std::uint8_t> sha256_file(const std::filesystem::path& path) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD object_size = 0;
    DWORD property_size = 0;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                          reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size),
                          &property_size, 0) < 0) {
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("Could not initialize SHA-256");
    }
    std::vector<std::uint8_t> object(object_size);
    std::vector<std::uint8_t> digest(32);
    if (BCryptCreateHash(algorithm, &hash, object.data(),
                         static_cast<ULONG>(object.size()), nullptr, 0, 0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("Could not initialize SHA-256 hash");
    }
    try {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("Could not open file for SHA-256");
        std::vector<char> buffer(1024 * 1024);
        while (input) {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto count = input.gcount();
            if (count > 0 && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()),
                                            static_cast<ULONG>(count), 0) < 0)
                throw std::runtime_error("Could not calculate SHA-256");
        }
        if (!input.eof()) throw std::runtime_error("Could not read file for SHA-256");
        if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
            throw std::runtime_error("Could not finish SHA-256");
    } catch (...) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw;
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return digest;
}

} // namespace homecloud::crypto
#else
#error A secure PBKDF2 provider must be added for this platform before building HomeCloud.
#endif
