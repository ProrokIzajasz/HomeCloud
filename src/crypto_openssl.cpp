#include "homecloud/crypto.hpp"

#include <limits>
#include <stdexcept>
#include <fstream>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

using namespace std;

namespace homecloud::crypto {
namespace {

int checked_size(size_t size) {
    if (size > static_cast<size_t>(numeric_limits<int>::max())) {
        throw invalid_argument("Cryptographic input is too large");
    }
    return static_cast<int>(size);
}

uint8_t decode_nibble(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    throw invalid_argument("Invalid hexadecimal value");
}

} // namespace

vector<uint8_t> random_bytes(size_t count) {
    vector<uint8_t> result(count);
    if (RAND_priv_bytes(result.data(), checked_size(result.size())) != 1) {
        throw runtime_error("Secure random generation failed");
    }
    return result;
}

vector<uint8_t> pbkdf2_sha256(string_view password,
                              span<const uint8_t> salt,
                              uint32_t iterations,
                              size_t output_size) {
    vector<uint8_t> result(output_size);
    if (iterations == 0 || iterations > static_cast<uint32_t>(numeric_limits<int>::max())) {
        throw invalid_argument("Invalid PBKDF2 iteration count");
    }
    if (PKCS5_PBKDF2_HMAC(password.data(), checked_size(password.size()),
                          salt.data(), checked_size(salt.size()),
                          static_cast<int>(iterations), EVP_sha256(),
                          checked_size(result.size()), result.data()) != 1) {
        throw runtime_error("PBKDF2 password hashing failed");
    }
    return result;
}

bool constant_time_equal(span<const uint8_t> left,
                         span<const uint8_t> right) noexcept {
    return left.size() == right.size() &&
           CRYPTO_memcmp(left.data(), right.data(), left.size()) == 0;
}

string hex_encode(span<const uint8_t> bytes) {
    constexpr char digits[] = "0123456789abcdef";
    string result(bytes.size() * 2, '0');
    for (size_t index = 0; index < bytes.size(); ++index) {
        result[index * 2] = digits[bytes[index] >> 4];
        result[index * 2 + 1] = digits[bytes[index] & 0x0f];
    }
    return result;
}

vector<uint8_t> hex_decode(string_view text) {
    if (text.size() % 2 != 0) throw invalid_argument("Invalid hexadecimal value");
    vector<uint8_t> result(text.size() / 2);
    for (size_t index = 0; index < result.size(); ++index) {
        result[index] = static_cast<uint8_t>(
            (decode_nibble(text[index * 2]) << 4) | decode_nibble(text[index * 2 + 1]));
    }
    return result;
}

vector<uint8_t> sha256_file(const filesystem::path& path) {
    ifstream input(path, ios::binary);
    if (!input) throw runtime_error("Could not open file for SHA-256");
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    if (!context || EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1) {
        if (context) EVP_MD_CTX_free(context);
        throw runtime_error("Could not initialize SHA-256");
    }
    vector<char> buffer(1024 * 1024);
    while (input) {
        input.read(buffer.data(), static_cast<streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0 && EVP_DigestUpdate(context, buffer.data(), static_cast<size_t>(count)) != 1) {
            EVP_MD_CTX_free(context);
            throw runtime_error("Could not calculate SHA-256");
        }
    }
    if (!input.eof()) {
        EVP_MD_CTX_free(context);
        throw runtime_error("Could not read file for SHA-256");
    }
    vector<uint8_t> digest(32);
    unsigned int size = 0;
    if (EVP_DigestFinal_ex(context, digest.data(), &size) != 1 || size != digest.size()) {
        EVP_MD_CTX_free(context);
        throw runtime_error("Could not finish SHA-256");
    }
    EVP_MD_CTX_free(context);
    return digest;
}

} // namespace homecloud::crypto
