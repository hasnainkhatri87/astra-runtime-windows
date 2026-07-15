#include "astra/android_manifest.hpp"

#include <zlib.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kNoIndex = 0xffffffffU;

void Append16(std::vector<unsigned char>& output, std::uint16_t value) {
    output.push_back(static_cast<unsigned char>(value));
    output.push_back(static_cast<unsigned char>(value >> 8U));
}

void Append32(std::vector<unsigned char>& output, std::uint32_t value) {
    output.push_back(static_cast<unsigned char>(value));
    output.push_back(static_cast<unsigned char>(value >> 8U));
    output.push_back(static_cast<unsigned char>(value >> 16U));
    output.push_back(static_cast<unsigned char>(value >> 24U));
}

void Store32(std::vector<unsigned char>& output, std::size_t offset,
             std::uint32_t value) {
    output[offset] = static_cast<unsigned char>(value);
    output[offset + 1] = static_cast<unsigned char>(value >> 8U);
    output[offset + 2] = static_cast<unsigned char>(value >> 16U);
    output[offset + 3] = static_cast<unsigned char>(value >> 24U);
}

void Pad4(std::vector<unsigned char>& output) {
    while ((output.size() & 3U) != 0) {
        output.push_back(0);
    }
}

void AppendLength8(std::vector<unsigned char>& output, std::size_t length) {
    if (length > 0x7fffU) {
        throw std::runtime_error("Synthetic string is too long");
    }
    if (length > 0x7fU) {
        output.push_back(static_cast<unsigned char>(0x80U | (length >> 8U)));
    }
    output.push_back(static_cast<unsigned char>(length));
}

std::vector<unsigned char> BuildStringPool(
    const std::vector<std::string>& strings) {
    std::vector<unsigned char> data;
    std::vector<std::uint32_t> offsets;
    for (const auto& value : strings) {
        offsets.push_back(static_cast<std::uint32_t>(data.size()));
        AppendLength8(data, value.size());
        AppendLength8(data, value.size());
        data.insert(data.end(), value.begin(), value.end());
        data.push_back(0);
    }

    std::vector<unsigned char> chunk;
    Append16(chunk, 0x0001U);
    Append16(chunk, 28);
    Append32(chunk, 0);
    Append32(chunk, static_cast<std::uint32_t>(strings.size()));
    Append32(chunk, 0);
    Append32(chunk, 1U << 8U);
    Append32(chunk, 28U + static_cast<std::uint32_t>(strings.size()) * 4U);
    Append32(chunk, 0);
    for (const auto offset : offsets) {
        Append32(chunk, offset);
    }
    chunk.insert(chunk.end(), data.begin(), data.end());
    Pad4(chunk);
    Store32(chunk, 4, static_cast<std::uint32_t>(chunk.size()));
    return chunk;
}

std::vector<unsigned char> BuildResourceMap(
    const std::vector<std::uint32_t>& identifiers) {
    std::vector<unsigned char> chunk;
    Append16(chunk, 0x0180U);
    Append16(chunk, 8);
    Append32(chunk, 8U + static_cast<std::uint32_t>(identifiers.size()) * 4U);
    for (const auto identifier : identifiers) {
        Append32(chunk, identifier);
    }
    return chunk;
}

struct AttributeSpec {
    std::uint32_t namespace_index{kNoIndex};
    std::uint32_t name_index{};
    std::uint32_t raw_value{kNoIndex};
    std::uint8_t type{};
    std::uint32_t data{};
};

std::vector<unsigned char> StartElement(
    std::uint32_t name_index,
    const std::vector<AttributeSpec>& attributes) {
    std::vector<unsigned char> chunk;
    Append16(chunk, 0x0102U);
    Append16(chunk, 16);
    Append32(chunk, 0);
    Append32(chunk, 1);
    Append32(chunk, kNoIndex);
    Append32(chunk, kNoIndex);
    Append32(chunk, name_index);
    Append16(chunk, 20);
    Append16(chunk, 20);
    Append16(chunk, static_cast<std::uint16_t>(attributes.size()));
    Append16(chunk, 0);
    Append16(chunk, 0);
    Append16(chunk, 0);
    for (const auto& attribute : attributes) {
        Append32(chunk, attribute.namespace_index);
        Append32(chunk, attribute.name_index);
        Append32(chunk, attribute.raw_value);
        Append16(chunk, 8);
        chunk.push_back(0);
        chunk.push_back(attribute.type);
        Append32(chunk, attribute.data);
    }
    Store32(chunk, 4, static_cast<std::uint32_t>(chunk.size()));
    return chunk;
}

std::vector<unsigned char> EndElement(std::uint32_t name_index) {
    std::vector<unsigned char> chunk;
    Append16(chunk, 0x0103U);
    Append16(chunk, 16);
    Append32(chunk, 24);
    Append32(chunk, 1);
    Append32(chunk, kNoIndex);
    Append32(chunk, kNoIndex);
    Append32(chunk, name_index);
    return chunk;
}

void AppendChunk(std::vector<unsigned char>& document,
                 const std::vector<unsigned char>& chunk) {
    document.insert(document.end(), chunk.begin(), chunk.end());
}

std::vector<unsigned char> BuildManifest() {
    const std::vector<std::string> strings = {
        "manifest",
        "package",
        "com.example.probe",
        "versionCode",
        "versionName",
        "1.2",
        "uses-sdk",
        "minSdkVersion",
        "targetSdkVersion",
        "application",
        "name",
        ".App",
        "activity",
        ".MainActivity",
        "intent-filter",
        "action",
        "android.intent.action.MAIN",
        "category",
        "android.intent.category.LAUNCHER",
        "http://schemas.android.com/apk/res/android",
    };
    std::vector<std::uint32_t> resources(strings.size(), 0);
    resources[3] = 0x0101021bU;
    resources[4] = 0x0101021cU;
    resources[7] = 0x0101020cU;
    resources[8] = 0x01010270U;
    resources[10] = 0x01010003U;

    constexpr std::uint32_t android_namespace = 19;
    std::vector<unsigned char> document;
    Append16(document, 0x0003U);
    Append16(document, 8);
    Append32(document, 0);
    AppendChunk(document, BuildStringPool(strings));
    AppendChunk(document, BuildResourceMap(resources));

    AppendChunk(document, StartElement(0, {
        {kNoIndex, 1, 2, 0x03, 2},
        {android_namespace, 3, kNoIndex, 0x10, 42},
        {android_namespace, 4, 5, 0x03, 5},
    }));
    AppendChunk(document, StartElement(6, {
        {android_namespace, 7, kNoIndex, 0x10, 29},
        {android_namespace, 8, kNoIndex, 0x10, 35},
    }));
    AppendChunk(document, EndElement(6));
    AppendChunk(document, StartElement(9, {
        {android_namespace, 10, 11, 0x03, 11},
    }));
    AppendChunk(document, StartElement(12, {
        {android_namespace, 10, 13, 0x03, 13},
    }));
    AppendChunk(document, StartElement(14, {}));
    AppendChunk(document, StartElement(15, {
        {android_namespace, 10, 16, 0x03, 16},
    }));
    AppendChunk(document, EndElement(15));
    AppendChunk(document, StartElement(17, {
        {android_namespace, 10, 18, 0x03, 18},
    }));
    AppendChunk(document, EndElement(17));
    AppendChunk(document, EndElement(14));
    AppendChunk(document, EndElement(12));
    AppendChunk(document, EndElement(9));
    AppendChunk(document, EndElement(0));
    Store32(document, 4, static_cast<std::uint32_t>(document.size()));
    return document;
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
                      L"astra-android-manifest-tests";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);

    const auto manifest_bytes = BuildManifest();
    const auto manifest_path = root / L"AndroidManifest.xml";
    {
        std::ofstream output(manifest_path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(manifest_bytes.data()),
                     static_cast<std::streamsize>(manifest_bytes.size()));
    }
    const auto parsed = astra::ParseAndroidBinaryManifest(manifest_path);
    Expect(parsed.ok, "valid binary manifest was rejected", failures);
    Expect(parsed.package_name == L"com.example.probe" &&
               parsed.version_code == 42 && parsed.version_name == L"1.2",
           "manifest identity was parsed incorrectly", failures);
    Expect(parsed.min_sdk == 29 && parsed.target_sdk == 35,
           "manifest SDK range was parsed incorrectly", failures);
    Expect(parsed.application_class == L"com.example.probe.App" &&
               parsed.launcher_activity == L"com.example.probe.MainActivity" &&
               parsed.activity_count == 1 && parsed.launcher_count == 1,
           "manifest application or launcher class was parsed incorrectly",
           failures);

    const auto apk = root / L"base.apk";
    WriteStoredZip(apk, "AndroidManifest.xml", manifest_bytes);
    const auto from_apk = astra::ReadAndroidManifestFromApk(
        apk, root / L"installed");
    Expect(from_apk.ok &&
               std::filesystem::is_regular_file(from_apk.binary_manifest_path),
           "signed-base APK manifest extraction path failed", failures);

    auto corrupt = manifest_bytes;
    Store32(corrupt, 4, static_cast<std::uint32_t>(corrupt.size() + 4));
    const auto corrupt_path = root / L"corrupt.xml";
    {
        std::ofstream output(corrupt_path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(corrupt.data()),
                     static_cast<std::streamsize>(corrupt.size()));
    }
    Expect(!astra::ParseAndroidBinaryManifest(corrupt_path).ok,
           "binary manifest with an invalid root range was accepted", failures);

    const auto nested_apk = root / L"nested.apk";
    WriteStoredZip(nested_apk, "assets/AndroidManifest.xml", manifest_bytes);
    Expect(!astra::ReadAndroidManifestFromApk(
                nested_apk, root / L"nested-installed").ok,
           "nested non-root AndroidManifest.xml was accepted", failures);

    std::filesystem::remove_all(root, ignored);
    if (failures == 0) {
        std::cout << "All Astra Android manifest tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
