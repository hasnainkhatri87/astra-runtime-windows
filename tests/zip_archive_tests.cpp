#include "astra/package_bundle.hpp"
#include "astra/apk_signature.hpp"
#include "astra/zip_archive.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

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

void WriteEmptyStoredZip(const std::filesystem::path& path,
                         const std::string& name) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    Put32(output, 0x04034b50U);
    Put16(output, 20);
    Put16(output, 0);
    Put16(output, 0);
    Put16(output, 0);
    Put16(output, 0);
    Put32(output, 0);
    Put32(output, 0);
    Put32(output, 0);
    Put16(output, static_cast<std::uint16_t>(name.size()));
    Put16(output, 0);
    output.write(name.data(), static_cast<std::streamsize>(name.size()));

    const auto central_offset = static_cast<std::uint32_t>(output.tellp());
    Put32(output, 0x02014b50U);
    Put16(output, 20);
    Put16(output, 20);
    Put16(output, 0);
    Put16(output, 0);
    Put16(output, 0);
    Put16(output, 0);
    Put32(output, 0);
    Put32(output, 0);
    Put32(output, 0);
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

bool ThrowsWhenOpening(const std::filesystem::path& path) {
    try {
        (void)astra::ZipArchive::Open(path);
        return false;
    } catch (const std::exception&) {
        return true;
    }
}

}  // namespace

int main() {
    int failures = 0;
    const auto root = std::filesystem::temp_directory_path() / L"astra-zip-tests";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);

    const auto safe = root / L"safe.apkm";
    WriteEmptyStoredZip(safe, "base.apk");
    const auto archive = astra::ZipArchive::Open(safe);
    if (archive.entries().size() != 1 || archive.entries()[0].name != L"base.apk") {
        std::cerr << "FAIL: safe central directory was not parsed\n";
        ++failures;
    }
    const auto provision = astra::ProvisionPackageSplits(safe, root / L"installed");
    if (!provision.ok || provision.selected_apks.size() != 1 ||
        !std::filesystem::exists(provision.selected_apks[0])) {
        std::cerr << "FAIL: safe base split was not provisioned\n";
        ++failures;
    }

const auto unsigned_result =
        astra::VerifyApkV2Signature(provision.selected_apks[0]);
    if (unsigned_result.ok) {
        std::cerr << "FAIL: unsigned synthetic APK passed signature verification\n";
        ++failures;
    }
    const auto traversal = root / L"traversal.apkm";
    WriteEmptyStoredZip(traversal, "../escape.apk");
    if (!ThrowsWhenOpening(traversal)) {
        std::cerr << "FAIL: traversal entry was accepted\n";
        ++failures;
    }

    std::filesystem::remove_all(root, ignored);
    if (failures == 0) {
        std::cout << "All Astra ZIP archive tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
