#include "astra/sha256.hpp"

#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace astra {
namespace {

void CheckNt(NTSTATUS status, const char* operation) {
    if (!BCRYPT_SUCCESS(status)) {
        throw std::runtime_error(operation);
    }
}

struct AlgorithmCloser {
    void operator()(void* handle) const noexcept {
        if (handle != nullptr) {
            BCryptCloseAlgorithmProvider(
                static_cast<BCRYPT_ALG_HANDLE>(handle), 0);
        }
    }
};

struct HashCloser {
    void operator()(void* handle) const noexcept {
        if (handle != nullptr) {
            BCryptDestroyHash(static_cast<BCRYPT_HASH_HANDLE>(handle));
        }
    }
};

}  // namespace

std::wstring Sha256File(const std::filesystem::path& path) {
    BCRYPT_ALG_HANDLE raw_algorithm = nullptr;
    CheckNt(BCryptOpenAlgorithmProvider(
                &raw_algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0),
            "BCryptOpenAlgorithmProvider failed");
    std::unique_ptr<void, AlgorithmCloser> algorithm(raw_algorithm);

    DWORD object_size = 0;
    DWORD bytes_written = 0;
    CheckNt(BCryptGetProperty(raw_algorithm, BCRYPT_OBJECT_LENGTH,
                              reinterpret_cast<PUCHAR>(&object_size),
                              sizeof(object_size), &bytes_written, 0),
            "BCryptGetProperty(object length) failed");

    DWORD hash_size = 0;
    CheckNt(BCryptGetProperty(raw_algorithm, BCRYPT_HASH_LENGTH,
                              reinterpret_cast<PUCHAR>(&hash_size),
                              sizeof(hash_size), &bytes_written, 0),
            "BCryptGetProperty(hash length) failed");

    std::vector<unsigned char> object_buffer(object_size);
    std::vector<unsigned char> hash_buffer(hash_size);

    BCRYPT_HASH_HANDLE raw_hash = nullptr;
    CheckNt(BCryptCreateHash(raw_algorithm, &raw_hash, object_buffer.data(),
                             object_size, nullptr, 0, 0),
            "BCryptCreateHash failed");
    std::unique_ptr<void, HashCloser> hash(raw_hash);

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open package");
    }

    std::vector<char> buffer(1024 * 1024);
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0) {
            CheckNt(BCryptHashData(
                        raw_hash,
                        reinterpret_cast<PUCHAR>(buffer.data()),
                        static_cast<ULONG>(count), 0),
                    "BCryptHashData failed");
        }
    }

    CheckNt(BCryptFinishHash(raw_hash, hash_buffer.data(), hash_size, 0),
            "BCryptFinishHash failed");

    std::wostringstream output;
    output << std::hex << std::setfill(L'0');
    for (const auto byte : hash_buffer) {
        output << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return output.str();
}

}  // namespace astra
