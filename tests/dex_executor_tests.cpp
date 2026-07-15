#include "astra/dex_catalog.hpp"
#include "astra/dex_executor.hpp"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
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

void Append16(std::vector<unsigned char>& value, std::uint16_t number) {
    value.push_back(static_cast<unsigned char>(number));
    value.push_back(static_cast<unsigned char>(number >> 8U));
}

void Append32(std::vector<unsigned char>& value, std::uint32_t number) {
    value.push_back(static_cast<unsigned char>(number));
    value.push_back(static_cast<unsigned char>(number >> 8U));
    value.push_back(static_cast<unsigned char>(number >> 16U));
    value.push_back(static_cast<unsigned char>(number >> 24U));
}

void AppendUleb128(std::vector<unsigned char>& value, std::uint32_t number) {
    do {
        std::uint8_t byte = static_cast<std::uint8_t>(number & 0x7fU);
        number >>= 7U;
        if (number != 0) {
            byte |= 0x80U;
        }
        value.push_back(byte);
    } while (number != 0);
}

void AppendMapItem(std::vector<unsigned char>& value, std::uint16_t type,
                   std::uint32_t count, std::uint32_t offset) {
    Append16(value, type);
    Append16(value, 0);
    Append32(value, count);
    Append32(value, offset);
}

void CheckNt(NTSTATUS status, const char* message) {
    if (!BCRYPT_SUCCESS(status)) {
        throw std::runtime_error(message);
    }
}

void RefreshDexHashes(std::vector<unsigned char>& dex) {
    std::fill(dex.begin() + 8, dex.begin() + 32, 0);
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    CheckNt(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA1_ALGORITHM,
                                        nullptr, 0),
            "Unable to open SHA-1 for DEX fixture");
    DWORD object_size = 0;
    DWORD written = 0;
    CheckNt(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                              reinterpret_cast<PUCHAR>(&object_size),
                              sizeof(object_size), &written, 0),
            "Unable to size SHA-1 for DEX fixture");
    std::vector<unsigned char> object(object_size);
    BCRYPT_HASH_HANDLE hash = nullptr;
    CheckNt(BCryptCreateHash(algorithm, &hash, object.data(), object_size,
                             nullptr, 0, 0),
            "Unable to create SHA-1 for DEX fixture");
    CheckNt(BCryptHashData(hash, dex.data() + 32,
                           static_cast<ULONG>(dex.size() - 32), 0),
            "Unable to hash DEX fixture");
    std::array<unsigned char, 20> signature{};
    CheckNt(BCryptFinishHash(hash, signature.data(),
                             static_cast<ULONG>(signature.size()), 0),
            "Unable to finish DEX fixture SHA-1");
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    std::copy(signature.begin(), signature.end(), dex.begin() + 12);

    constexpr std::uint32_t kModAdler = 65521U;
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (std::size_t index = 12; index < dex.size(); ++index) {
        a = (a + dex[index]) % kModAdler;
        b = (b + a) % kModAdler;
    }
    Store32(dex, 8, (b << 16U) | a);
}

std::vector<unsigned char> BuildProbeDex(
    std::uint16_t register_count,
    const std::vector<std::uint16_t>& instructions) {
    constexpr std::uint32_t kStringIdsOffset = 0x70;
    constexpr std::uint32_t kTypeIdsOffset = 0x84;
    constexpr std::uint32_t kProtoIdsOffset = 0x90;
    constexpr std::uint32_t kMethodIdsOffset = 0x9c;
    constexpr std::uint32_t kClassDefsOffset = 0xa4;
    constexpr std::uint32_t kDataOffset = 0xc4;
    const std::array<std::string, 5> strings = {
        "AstraProbe.java", "I", "LAstraProbe;",
        "Ljava/lang/Object;", "answer"};

    std::vector<unsigned char> dex(kDataOffset, 0);
    const unsigned char magic[8] = {'d', 'e', 'x', '\n', '0', '3', '9', 0};
    std::copy(std::begin(magic), std::end(magic), dex.begin());
    Store32(dex, 0x24, 0x70);
    Store32(dex, 0x28, 0x12345678U);
    Store32(dex, 0x38, static_cast<std::uint32_t>(strings.size()));
    Store32(dex, 0x3c, kStringIdsOffset);
    Store32(dex, 0x40, 3);
    Store32(dex, 0x44, kTypeIdsOffset);
    Store32(dex, 0x48, 1);
    Store32(dex, 0x4c, kProtoIdsOffset);
    Store32(dex, 0x58, 1);
    Store32(dex, 0x5c, kMethodIdsOffset);
    Store32(dex, 0x60, 1);
    Store32(dex, 0x64, kClassDefsOffset);
    Store32(dex, 0x6c, kDataOffset);

    Store32(dex, kTypeIdsOffset, 1);
    Store32(dex, kTypeIdsOffset + 4, 2);
    Store32(dex, kTypeIdsOffset + 8, 3);
    Store32(dex, kProtoIdsOffset, 1);
    Store32(dex, kProtoIdsOffset + 4, 0);
    Store32(dex, kProtoIdsOffset + 8, 0);
    Store16(dex, kMethodIdsOffset, 1);
    Store16(dex, kMethodIdsOffset + 2, 0);
    Store32(dex, kMethodIdsOffset + 4, 4);
    Store32(dex, kClassDefsOffset, 1);
    Store32(dex, kClassDefsOffset + 4, 0x0001U);
    Store32(dex, kClassDefsOffset + 8, 2);
    Store32(dex, kClassDefsOffset + 16, 0);

    const std::uint32_t code_offset = static_cast<std::uint32_t>(dex.size());
    Append16(dex, register_count);
    Append16(dex, 0);
    Append16(dex, 0);
    Append16(dex, 0);
    Append32(dex, 0);
    Append32(dex, static_cast<std::uint32_t>(instructions.size()));
    for (const std::uint16_t instruction : instructions) {
        Append16(dex, instruction);
    }

    const std::uint32_t class_data_offset =
        static_cast<std::uint32_t>(dex.size());
    AppendUleb128(dex, 0);
    AppendUleb128(dex, 0);
    AppendUleb128(dex, 1);
    AppendUleb128(dex, 0);
    AppendUleb128(dex, 0);
    AppendUleb128(dex, 0x0009U);
    AppendUleb128(dex, code_offset);
    Store32(dex, kClassDefsOffset + 24, class_data_offset);

    const std::uint32_t first_string_offset =
        static_cast<std::uint32_t>(dex.size());
    for (std::size_t index = 0; index < strings.size(); ++index) {
        Store32(dex, kStringIdsOffset + index * 4,
                static_cast<std::uint32_t>(dex.size()));
        AppendUleb128(dex, static_cast<std::uint32_t>(strings[index].size()));
        dex.insert(dex.end(), strings[index].begin(), strings[index].end());
        dex.push_back(0);
    }
    while ((dex.size() & 3U) != 0) {
        dex.push_back(0);
    }

    const std::uint32_t map_offset = static_cast<std::uint32_t>(dex.size());
    Append32(dex, 10);
    AppendMapItem(dex, 0x0000U, 1, 0);
    AppendMapItem(dex, 0x0001U, 5, kStringIdsOffset);
    AppendMapItem(dex, 0x0002U, 3, kTypeIdsOffset);
    AppendMapItem(dex, 0x0003U, 1, kProtoIdsOffset);
    AppendMapItem(dex, 0x0005U, 1, kMethodIdsOffset);
    AppendMapItem(dex, 0x0006U, 1, kClassDefsOffset);
    AppendMapItem(dex, 0x2001U, 1, code_offset);
    AppendMapItem(dex, 0x2000U, 1, class_data_offset);
    AppendMapItem(dex, 0x2002U, 5, first_string_offset);
    AppendMapItem(dex, 0x1000U, 1, map_offset);

    Store32(dex, 0x20, static_cast<std::uint32_t>(dex.size()));
    Store32(dex, 0x34, map_offset);
    Store32(dex, 0x68,
            static_cast<std::uint32_t>(dex.size()) - kDataOffset);
    RefreshDexHashes(dex);
    return dex;
}

void WriteBytes(const std::filesystem::path& path,
                const std::vector<unsigned char>& value) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(value.data()),
                 static_cast<std::streamsize>(value.size()));
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
                      L"astra-dex-executor-tests";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);

    const auto answer_path = root / L"answer.dex";
    WriteBytes(answer_path, BuildProbeDex(2, {
        0x0013U, 40U,       // const/16 v0, #40
        0x01d8U, 0x0200U,  // add-int/lit8 v1, v0, #2
        0x010fU,            // return v1
    }));
    try {
        const auto verified =
            astra::ValidateDexFile(answer_path, L"answer.dex");
        Expect(verified.version == L"039" && verified.method_ids == 1 &&
                   verified.class_defs == 1,
               "synthetic proof DEX did not pass the production verifier",
               failures);
    } catch (const std::exception&) {
        Expect(false, "synthetic proof DEX validation threw", failures);
    }
    const auto answer = astra::ExecuteStaticIntMethod(
        answer_path, "LAstraProbe;", "answer");
    Expect(answer.ok && answer.value == 42 &&
               answer.instructions_executed == 3,
           "real DEX arithmetic method did not return 42", failures);

    const auto branch_path = root / L"branch.dex";
    WriteBytes(branch_path, BuildProbeDex(3, {
        0x3012U,             // const/4 v0, #3
        0x2112U,             // const/4 v1, #2
        0x1036U, 0x0004U,   // if-gt v0, v1, +4
        0x0212U,             // const/4 v2, #0
        0x0228U,             // goto +2
        0x7212U,             // const/4 v2, #7
        0x020fU,             // return v2
    }));
    const auto branch = astra::ExecuteStaticIntMethod(
        branch_path, "LAstraProbe;", "answer");
    Expect(branch.ok && branch.value == 7 &&
               branch.instructions_executed == 5,
           "DEX conditional branch did not select the expected path", failures);

    const auto divide_path = root / L"divide-zero.dex";
    WriteBytes(divide_path, BuildProbeDex(3, {
        0x1012U,             // const/4 v0, #1
        0x0112U,             // const/4 v1, #0
        0x0293U, 0x0100U,   // div-int v2, v0, v1
        0x020fU,
    }));
    const auto divide = astra::ExecuteStaticIntMethod(
        divide_path, "LAstraProbe;", "answer");
    Expect(!divide.ok &&
               divide.message.find(L"division by zero") != std::wstring::npos,
           "DEX division by zero did not fail safely", failures);

    const auto unsupported_path = root / L"unsupported.dex";
    WriteBytes(unsupported_path, BuildProbeDex(1, {
        0x001aU, 0x0000U,  // const-string is outside the proof subset
        0x000fU,
    }));
    const auto unsupported = astra::ExecuteStaticIntMethod(
        unsupported_path, "LAstraProbe;", "answer");
    Expect(!unsupported.ok &&
               unsupported.message.find(L"opcode") != std::wstring::npos,
           "unsupported DEX opcode was not rejected", failures);

    const auto loop_path = root / L"loop.dex";
    WriteBytes(loop_path, BuildProbeDex(1, {
        0x002aU, 0x0000U, 0x0000U,  // goto/32 +0
    }));
    const auto loop = astra::ExecuteStaticIntMethod(
        loop_path, "LAstraProbe;", "answer");
    Expect(!loop.ok &&
               loop.message.find(L"step limit") != std::wstring::npos,
           "DEX infinite loop did not hit the execution limit", failures);

    auto corrupted_bytes = BuildProbeDex(2, {
        0x0013U, 40U, 0x01d8U, 0x0200U, 0x010fU});
    // Change the const/16 literal without refreshing the DEX hashes. This
    // leaves all structural offsets intact and isolates integrity rejection.
    corrupted_bytes[0xc4U + 18U] ^= 1U;
    const auto corrupted_path = root / L"corrupted.dex";
    WriteBytes(corrupted_path, corrupted_bytes);
    const auto corrupted = astra::ExecuteStaticIntMethod(
        corrupted_path, "LAstraProbe;", "answer");
    Expect(!corrupted.ok &&
               corrupted.message.find(L"checksum") != std::wstring::npos,
           "tampered DEX did not fail integrity verification", failures);

    const auto missing = astra::ExecuteStaticIntMethod(
        answer_path, "LMissing;", "answer");
    Expect(!missing.ok, "missing DEX class unexpectedly executed", failures);

    std::filesystem::remove_all(root, ignored);
    if (failures == 0) {
        std::cout << "All Astra real-DEX execution tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
