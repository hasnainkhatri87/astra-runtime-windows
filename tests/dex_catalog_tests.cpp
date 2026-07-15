#include "astra/dex_catalog.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <zlib.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void Store16(std::vector<unsigned char>& value, std::size_t offset,
             std::uint16_t number) {
    value[offset] = static_cast<unsigned char>(number);
    value[offset + 1] = static_cast<unsigned char>(number >> 8U);
}

void Store32(std::vector<unsigned char>& value, std::size_t offset,
             std::uint32_t number) {
    value[offset] = static_cast<unsigned char>(number);
    value[offset + 1] = static_cast<unsigned char>(number >> 8U);
    value[offset + 2] = static_cast<unsigned char>(number >> 16U);
    value[offset + 3] = static_cast<unsigned char>(number >> 24U);
}

void Put16(std::ofstream& output, std::uint16_t value) {
    const unsigned char bytes[] = {
        static_cast<unsigned char>(value),
        static_cast<unsigned char>(value >> 8U)};
    output.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

void Put32(std::ofstream& output, std::uint32_t value) {
    const unsigned char bytes[] = {
        static_cast<unsigned char>(value),
        static_cast<unsigned char>(value >> 8U),
        static_cast<unsigned char>(value >> 16U),
        static_cast<unsigned char>(value >> 24U)};
    output.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

void FinalizeDex(std::vector<unsigned char>& dex) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
            &algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0))) {
        throw std::runtime_error("Unable to open test SHA-1 provider");
    }
    const NTSTATUS hash_status = BCryptHash(
        algorithm, nullptr, 0, dex.data() + 32,
        static_cast<ULONG>(dex.size() - 32), dex.data() + 12, 20);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!BCRYPT_SUCCESS(hash_status)) {
        throw std::runtime_error("Unable to hash synthetic DEX");
    }

    uLong checksum = adler32(0L, Z_NULL, 0);
    checksum = adler32(checksum, dex.data() + 12,
                       static_cast<uInt>(dex.size() - 12));
    Store32(dex, 8, static_cast<std::uint32_t>(checksum));
}

std::vector<unsigned char> BuildMinimalDex(const std::string& version = "039") {
    constexpr std::uint32_t header_size = 0x70;
    constexpr std::uint32_t map_size = 28;
    std::vector<unsigned char> dex(header_size + map_size, 0);
    dex[0] = 'd';
    dex[1] = 'e';
    dex[2] = 'x';
    dex[3] = '\n';
    dex[4] = static_cast<unsigned char>(version.at(0));
    dex[5] = static_cast<unsigned char>(version.at(1));
    dex[6] = static_cast<unsigned char>(version.at(2));
    dex[7] = 0;
    Store32(dex, 0x20, static_cast<std::uint32_t>(dex.size()));
    Store32(dex, 0x24, header_size);
    Store32(dex, 0x28, 0x12345678U);
    Store32(dex, 0x34, header_size);
    Store32(dex, 0x68, map_size);
    Store32(dex, 0x6c, header_size);

    Store32(dex, header_size, 2);
    Store16(dex, header_size + 4, 0x0000U);
    Store32(dex, header_size + 8, 1);
    Store32(dex, header_size + 12, 0);
    Store16(dex, header_size + 16, 0x1000U);
    Store32(dex, header_size + 20, 1);
    Store32(dex, header_size + 24, header_size);
    FinalizeDex(dex);
    return dex;
}

void WriteStoredZip(const std::filesystem::path& path,
                    const std::string& name,
                    const std::vector<unsigned char>& payload) {
    const auto size = static_cast<std::uint32_t>(payload.size());
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, payload.data(), static_cast<uInt>(payload.size()));

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    Put32(output, 0x04034b50U);
    Put16(output, 20);
    Put16(output, 0);
    Put16(output, 0);
    Put16(output, 0);
    Put16(output, 0);
    Put32(output, static_cast<std::uint32_t>(crc));
    Put32(output, size);
    Put32(output, size);
    Put16(output, static_cast<std::uint16_t>(name.size()));
    Put16(output, 0);
    output.write(name.data(), static_cast<std::streamsize>(name.size()));
    output.write(reinterpret_cast<const char*>(payload.data()),
                 static_cast<std::streamsize>(payload.size()));

    const auto central_offset = static_cast<std::uint32_t>(output.tellp());
    Put32(output, 0x02014b50U);
    Put16(output, 20);
    Put16(output, 20);
    Put16(output, 0);
    Put16(output, 0);
    Put16(output, 0);
    Put16(output, 0);
    Put32(output, static_cast<std::uint32_t>(crc));
    Put32(output, size);
    Put32(output, size);
    Put16(output, static_cast<std::uint16_t>(name.size()));
    Put16(output, 0);
    Put16(output, 0);
    Put16(output, 0);
    Put16(output, 0);
    Put32(output, 0);
    Put32(output, 0);
    output.write(name.data(), static_cast<std::streamsize>(name.size()));

    const auto central_end = static_cast<std::uint32_t>(output.tellp());
    Put32(output, 0x06054b50U);
    Put16(output, 0);
    Put16(output, 0);
    Put16(output, 1);
    Put16(output, 1);
    Put32(output, central_end - central_offset);
    Put32(output, central_offset);
    Put16(output, 0);
}

void Expect(bool condition, const char* message, int& failures) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

}  // namespace

int main() {
    int failures = 0;
    const auto root = std::filesystem::temp_directory_path() /
                      L"astra-dex-catalog-tests";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);

    const auto valid_apk = root / L"valid.apk";
    WriteStoredZip(valid_apk, "classes.dex", BuildMinimalDex());
    const auto valid_root = root / L"valid-app";
    const auto valid = astra::CatalogDexPayloads({valid_apk}, valid_root);
    Expect(valid.ok && valid.dex_files.size() == 1,
           "valid synthetic DEX was not cataloged", failures);
    Expect(std::filesystem::is_regular_file(
               valid_root / L"runtime" / L"dex" / L"catalog.tsv"),
           "DEX catalog file was not written", failures);

    const auto extracted = valid_root / L"runtime" / L"dex" /
                           L"0-classes.dex";
    {
        std::fstream corrupt(extracted, std::ios::binary | std::ios::in |
                                           std::ios::out);
        corrupt.seekp(40);
        corrupt.put(static_cast<char>(0xff));
    }
    const auto repaired = astra::CatalogDexPayloads({valid_apk}, valid_root);
    Expect(repaired.ok,
           "cataloging did not replace a stale extracted DEX", failures);

    auto bad_checksum = BuildMinimalDex();
    bad_checksum.back() ^= 0x01U;
    const auto corrupt_apk = root / L"corrupt.apk";
    WriteStoredZip(corrupt_apk, "classes.dex", bad_checksum);
    const auto corrupt_result = astra::CatalogDexPayloads(
        {corrupt_apk}, root / L"corrupt-app");
    Expect(!corrupt_result.ok,
           "DEX with invalid signed checksum fields was accepted", failures);

    const auto future_apk = root / L"future.apk";
    WriteStoredZip(future_apk, "classes.dex", BuildMinimalDex("041"));
    const auto future_result = astra::CatalogDexPayloads(
        {future_apk}, root / L"future-app");
    Expect(!future_result.ok,
           "unsupported v041 DEX container was silently accepted", failures);

    const auto nested_apk = root / L"nested.apk";
    WriteStoredZip(nested_apk, "assets/classes.dex", BuildMinimalDex());
    const auto nested_result = astra::CatalogDexPayloads(
        {nested_apk}, root / L"nested-app");
    Expect(!nested_result.ok,
           "non-root classes.dex was treated as executable code", failures);

    std::filesystem::remove_all(root, ignored);
    if (failures == 0) {
        std::cout << "All Astra DEX catalog tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
