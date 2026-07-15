#include "astra/apk_signature.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace astra {
namespace {

constexpr std::uint32_t kEndOfCentralDirectory = 0x06054b50U;
constexpr std::uint32_t kV2BlockId = 0x7109871aU;
constexpr std::uint32_t kRsaPkcs1Sha256 = 0x0103U;
constexpr std::size_t kChunkBytes = 1024 * 1024;
constexpr std::array<unsigned char, 16> kSigningBlockMagic = {
    'A', 'P', 'K', ' ', 'S', 'i', 'g', ' ', 'B', 'l', 'o', 'c', 'k', ' ', '4', '2'};

std::uint16_t U16(const unsigned char* value) {
    return static_cast<std::uint16_t>(value[0]) |
           (static_cast<std::uint16_t>(value[1]) << 8U);
}

std::uint32_t U32(const unsigned char* value) {
    return static_cast<std::uint32_t>(value[0]) |
           (static_cast<std::uint32_t>(value[1]) << 8U) |
           (static_cast<std::uint32_t>(value[2]) << 16U) |
           (static_cast<std::uint32_t>(value[3]) << 24U);
}

std::uint64_t U64(const unsigned char* value) {
    return static_cast<std::uint64_t>(U32(value)) |
           (static_cast<std::uint64_t>(U32(value + 4)) << 32U);
}

void Put32(std::vector<unsigned char>& output, std::uint32_t value) {
    output.push_back(static_cast<unsigned char>(value));
    output.push_back(static_cast<unsigned char>(value >> 8U));
    output.push_back(static_cast<unsigned char>(value >> 16U));
    output.push_back(static_cast<unsigned char>(value >> 24U));
}

void ReadExact(std::ifstream& input, void* output, std::size_t size) {
    input.read(static_cast<char*>(output), static_cast<std::streamsize>(size));
    if (input.gcount() != static_cast<std::streamsize>(size)) {
        throw std::runtime_error("Unexpected end of APK");
    }
}

class ByteReader {
public:
    explicit ByteReader(std::vector<unsigned char> bytes)
        : storage_(std::move(bytes)), data_(storage_.data()), size_(storage_.size()) {}

    ByteReader(const unsigned char* data, std::size_t size)
        : data_(data), size_(size) {}

    std::uint32_t ReadU32() {
        Require(4);
        const auto value = U32(data_ + position_);
        position_ += 4;
        return value;
    }

    ByteReader ReadLengthPrefixed() {
        const std::uint32_t length = ReadU32();
        Require(length);
        ByteReader result(data_ + position_, length);
        position_ += length;
        return result;
    }

    std::vector<unsigned char> ReadRemaining() {
        std::vector<unsigned char> result(data_ + position_, data_ + size_);
        position_ = size_;
        return result;
    }

    std::vector<unsigned char> CopyBytes() const {
        return {data_, data_ + size_};
    }

    bool empty() const noexcept { return position_ == size_; }
    std::size_t remaining() const noexcept { return size_ - position_; }

private:
    void Require(std::size_t count) const {
        if (count > size_ - position_) {
            throw std::runtime_error("Invalid length-prefixed APK signing record");
        }
    }

    std::vector<unsigned char> storage_;
    const unsigned char* data_{};
    std::size_t size_{};
    std::size_t position_{};
};

struct AlgorithmCloser {
    void operator()(void* handle) const noexcept {
        if (handle) {
            BCryptCloseAlgorithmProvider(static_cast<BCRYPT_ALG_HANDLE>(handle), 0);
        }
    }
};

struct HashCloser {
    void operator()(void* handle) const noexcept {
        if (handle) {
            BCryptDestroyHash(static_cast<BCRYPT_HASH_HANDLE>(handle));
        }
    }
};

struct KeyCloser {
    void operator()(void* handle) const noexcept {
        if (handle) {
            BCryptDestroyKey(static_cast<BCRYPT_KEY_HANDLE>(handle));
        }
    }
};

struct CertificateCloser {
    void operator()(const CERT_CONTEXT* certificate) const noexcept {
        if (certificate) {
            CertFreeCertificateContext(certificate);
        }
    }
};

void CheckNt(NTSTATUS status, const char* operation) {
    if (!BCRYPT_SUCCESS(status)) {
        throw std::runtime_error(operation);
    }
}

class Sha256 {
public:
    Sha256() {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        CheckNt(BCryptOpenAlgorithmProvider(
                    &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0),
                "Unable to open SHA-256 provider");
        algorithm_.reset(algorithm);

        DWORD object_size = 0;
        DWORD written = 0;
        CheckNt(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                                  reinterpret_cast<PUCHAR>(&object_size),
                                  sizeof(object_size), &written, 0),
                "Unable to get SHA-256 object length");
        object_.resize(object_size);

        BCRYPT_HASH_HANDLE hash = nullptr;
        CheckNt(BCryptCreateHash(algorithm, &hash, object_.data(), object_size,
                                 nullptr, 0, 0),
                "Unable to create SHA-256 hash");
        hash_.reset(hash);
    }

    void Update(const void* data, std::size_t size) {
        const auto* cursor = static_cast<const unsigned char*>(data);
        while (size != 0) {
            const ULONG amount = static_cast<ULONG>(
                std::min<std::size_t>(size, std::numeric_limits<ULONG>::max()));
            CheckNt(BCryptHashData(hash_.get(), const_cast<PUCHAR>(cursor), amount, 0),
                    "Unable to update SHA-256 hash");
            cursor += amount;
            size -= amount;
        }
    }

    std::array<unsigned char, 32> Finish() {
        std::array<unsigned char, 32> result{};
        CheckNt(BCryptFinishHash(hash_.get(), result.data(),
                                 static_cast<ULONG>(result.size()), 0),
                "Unable to finish SHA-256 hash");
        return result;
    }

private:
    std::unique_ptr<void, AlgorithmCloser> algorithm_;
    std::unique_ptr<void, HashCloser> hash_;
    std::vector<unsigned char> object_;
};

std::array<unsigned char, 32> HashBytes(const void* data, std::size_t size) {
    Sha256 hash;
    hash.Update(data, size);
    return hash.Finish();
}

std::wstring Hex(const unsigned char* data, std::size_t size) {
    std::wostringstream output;
    output << std::hex << std::setfill(L'0');
    for (std::size_t index = 0; index < size; ++index) {
        output << std::setw(2) << static_cast<unsigned int>(data[index]);
    }
    return output.str();
}

struct ApkLayout {
    std::uint64_t file_size{};
    std::uint64_t central_offset{};
    std::uint64_t eocd_offset{};
    std::uint64_t signing_block_offset{};
    std::vector<unsigned char> eocd;
    std::vector<unsigned char> v2_block;
};

ApkLayout ReadLayout(const std::filesystem::path& path) {
    ApkLayout layout;
    layout.file_size = std::filesystem::file_size(path);
    if (layout.file_size < 22) {
        throw std::runtime_error("APK is too small");
    }

    std::ifstream input(path, std::ios::binary);
    const std::uint64_t tail_size = std::min<std::uint64_t>(
        layout.file_size, 22ULL + 65535ULL);
    std::vector<unsigned char> tail(static_cast<std::size_t>(tail_size));
    input.seekg(static_cast<std::streamoff>(layout.file_size - tail_size));
    ReadExact(input, tail.data(), tail.size());

    std::size_t eocd_in_tail = std::numeric_limits<std::size_t>::max();
    for (std::size_t index = tail.size() - 22;; --index) {
        if (U32(tail.data() + index) == kEndOfCentralDirectory) {
            const auto comment = U16(tail.data() + index + 20);
            if (index + 22ULL + comment == tail.size()) {
                eocd_in_tail = index;
                break;
            }
        }
        if (index == 0) {
            break;
        }
    }
    if (eocd_in_tail == std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("APK ZIP footer is missing");
    }

    layout.eocd_offset = layout.file_size - tail_size + eocd_in_tail;
    layout.eocd.assign(tail.begin() + static_cast<std::ptrdiff_t>(eocd_in_tail),
                       tail.end());
    if (U16(layout.eocd.data() + 4) != 0 || U16(layout.eocd.data() + 6) != 0) {
        throw std::runtime_error("Multi-disk APK is unsupported");
    }
    layout.central_offset = U32(layout.eocd.data() + 16);
    if (layout.central_offset < 32 || layout.central_offset > layout.eocd_offset) {
        throw std::runtime_error("APK central-directory offset is invalid");
    }

    std::array<unsigned char, 24> footer{};
    input.clear();
    input.seekg(static_cast<std::streamoff>(layout.central_offset - footer.size()));
    ReadExact(input, footer.data(), footer.size());
    if (!std::equal(kSigningBlockMagic.begin(), kSigningBlockMagic.end(),
                    footer.begin() + 8)) {
        throw std::runtime_error("APK v2 signing block is missing");
    }
    const std::uint64_t block_size_without_header = U64(footer.data());
    if (block_size_without_header < 24 ||
        block_size_without_header > layout.central_offset - 8) {
        throw std::runtime_error("APK signing block size is invalid");
    }
    layout.signing_block_offset =
        layout.central_offset - (block_size_without_header + 8);

    std::array<unsigned char, 8> header_size{};
    input.seekg(static_cast<std::streamoff>(layout.signing_block_offset));
    ReadExact(input, header_size.data(), header_size.size());
    if (U64(header_size.data()) != block_size_without_header) {
        throw std::runtime_error("APK signing block size fields disagree");
    }

    const std::uint64_t pairs_size = block_size_without_header - 24;
    if (pairs_size > 16ULL * 1024ULL * 1024ULL) {
        throw std::runtime_error("APK signing block is unreasonably large");
    }
    std::vector<unsigned char> pairs(static_cast<std::size_t>(pairs_size));
    ReadExact(input, pairs.data(), pairs.size());

    std::size_t position = 0;
    while (position < pairs.size()) {
        if (pairs.size() - position < 8) {
            throw std::runtime_error("Truncated APK signing pair length");
        }
        const std::uint64_t pair_size = U64(pairs.data() + position);
        position += 8;
        if (pair_size < 4 || pair_size > pairs.size() - position) {
            throw std::runtime_error("Invalid APK signing pair size");
        }
        const std::uint32_t id = U32(pairs.data() + position);
        if (id == kV2BlockId) {
            if (!layout.v2_block.empty()) {
                throw std::runtime_error("Duplicate APK v2 signing block");
            }
            layout.v2_block.assign(pairs.begin() + static_cast<std::ptrdiff_t>(position + 4),
                                   pairs.begin() + static_cast<std::ptrdiff_t>(position + pair_size));
        }
        position += static_cast<std::size_t>(pair_size);
    }
    if (layout.v2_block.empty()) {
        throw std::runtime_error("APK has no v2 signer record");
    }
    return layout;
}

std::array<unsigned char, 32> HashChunk(const unsigned char* data,
                                        std::size_t size) {
    std::array<unsigned char, 5> prefix{};
    prefix[0] = 0xa5U;
    prefix[1] = static_cast<unsigned char>(size);
    prefix[2] = static_cast<unsigned char>(size >> 8U);
    prefix[3] = static_cast<unsigned char>(size >> 16U);
    prefix[4] = static_cast<unsigned char>(size >> 24U);
    Sha256 hash;
    hash.Update(prefix.data(), prefix.size());
    hash.Update(data, size);
    return hash.Finish();
}

std::array<unsigned char, 32> ComputeContentDigest(
    const std::filesystem::path& path, const ApkLayout& layout) {
    std::vector<std::array<unsigned char, 32>> chunk_hashes;
    std::vector<unsigned char> buffer(kChunkBytes);
    std::ifstream input(path, std::ios::binary);

    const auto hash_file_range = [&](std::uint64_t offset, std::uint64_t size) {
        input.clear();
        input.seekg(static_cast<std::streamoff>(offset));
        while (size != 0) {
            const auto amount = static_cast<std::size_t>(
                std::min<std::uint64_t>(size, buffer.size()));
            ReadExact(input, buffer.data(), amount);
            chunk_hashes.push_back(HashChunk(buffer.data(), amount));
            size -= amount;
        }
    };

    hash_file_range(0, layout.signing_block_offset);
    hash_file_range(layout.central_offset,
                    layout.eocd_offset - layout.central_offset);

    auto patched_eocd = layout.eocd;
    if (layout.signing_block_offset > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("APK signing-block offset exceeds ZIP32 range");
    }
    const auto offset32 = static_cast<std::uint32_t>(layout.signing_block_offset);
    patched_eocd[16] = static_cast<unsigned char>(offset32);
    patched_eocd[17] = static_cast<unsigned char>(offset32 >> 8U);
    patched_eocd[18] = static_cast<unsigned char>(offset32 >> 16U);
    patched_eocd[19] = static_cast<unsigned char>(offset32 >> 24U);
    for (std::size_t position = 0; position < patched_eocd.size();) {
        const auto amount = std::min(kChunkBytes, patched_eocd.size() - position);
        chunk_hashes.push_back(HashChunk(patched_eocd.data() + position, amount));
        position += amount;
    }

    if (chunk_hashes.empty() ||
        chunk_hashes.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("Invalid APK content chunk count");
    }
    std::vector<unsigned char> aggregate;
    aggregate.reserve(5 + chunk_hashes.size() * 32);
    aggregate.push_back(0x5aU);
    Put32(aggregate, static_cast<std::uint32_t>(chunk_hashes.size()));
    for (const auto& digest : chunk_hashes) {
        aggregate.insert(aggregate.end(), digest.begin(), digest.end());
    }
    return HashBytes(aggregate.data(), aggregate.size());
}

struct ParsedSigner {
    std::vector<unsigned char> signed_data;
    std::vector<unsigned char> certificate;
    std::vector<unsigned char> public_key;
    std::vector<unsigned char> signature;
    std::vector<unsigned char> content_digest;
};

ParsedSigner ParseSigner(ByteReader signer) {
    ParsedSigner parsed;
    auto signed_data = signer.ReadLengthPrefixed();
    parsed.signed_data = signed_data.CopyBytes();
    auto signatures = signer.ReadLengthPrefixed();
    parsed.public_key = signer.ReadLengthPrefixed().CopyBytes();
    if (!signer.empty()) {
        throw std::runtime_error("Unexpected trailing bytes in APK signer");
    }

    std::set<std::uint32_t> signature_algorithms;
    while (!signatures.empty()) {
        auto record = signatures.ReadLengthPrefixed();
        const auto algorithm = record.ReadU32();
        auto signature = record.ReadLengthPrefixed().CopyBytes();
        if (!record.empty() || !signature_algorithms.insert(algorithm).second) {
            throw std::runtime_error("Invalid or duplicate APK signature record");
        }
        if (algorithm == kRsaPkcs1Sha256) {
            parsed.signature = std::move(signature);
        }
    }
    if (parsed.signature.empty()) {
        throw std::runtime_error("APK signer lacks supported RSA/SHA-256 signature");
    }

    auto digests = signed_data.ReadLengthPrefixed();
    auto certificates = signed_data.ReadLengthPrefixed();
    (void)signed_data.ReadLengthPrefixed();
    if (!signed_data.empty()) {
        throw std::runtime_error("Unexpected trailing bytes in APK signed data");
    }

    std::set<std::uint32_t> digest_algorithms;
    while (!digests.empty()) {
        auto record = digests.ReadLengthPrefixed();
        const auto algorithm = record.ReadU32();
        auto digest = record.ReadLengthPrefixed().CopyBytes();
        if (!record.empty() || !digest_algorithms.insert(algorithm).second) {
            throw std::runtime_error("Invalid or duplicate APK digest record");
        }
        if (algorithm == kRsaPkcs1Sha256) {
            parsed.content_digest = std::move(digest);
        }
    }
    if (parsed.content_digest.size() != 32) {
        throw std::runtime_error("APK signer lacks supported SHA-256 content digest");
    }
    if (certificates.empty()) {
        throw std::runtime_error("APK signer certificate list is empty");
    }
    parsed.certificate = certificates.ReadLengthPrefixed().CopyBytes();
    while (!certificates.empty()) {
        (void)certificates.ReadLengthPrefixed();
    }
    return parsed;
}

std::wstring VerifySigner(const ParsedSigner& signer,
                          const std::array<unsigned char, 32>& content_digest) {
    if (!std::equal(content_digest.begin(), content_digest.end(),
                    signer.content_digest.begin())) {
        throw std::runtime_error("APK signed content digest does not match file bytes");
    }

    const CERT_CONTEXT* raw_certificate = CertCreateCertificateContext(
        X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
        signer.certificate.data(), static_cast<DWORD>(signer.certificate.size()));
    if (!raw_certificate) {
        throw std::runtime_error("Unable to decode APK signer certificate");
    }
    std::unique_ptr<const CERT_CONTEXT, CertificateCloser> certificate(raw_certificate);

    DWORD public_key_size = 0;
    if (!CryptEncodeObjectEx(X509_ASN_ENCODING, X509_PUBLIC_KEY_INFO,
                             &raw_certificate->pCertInfo->SubjectPublicKeyInfo,
                             0, nullptr, nullptr, &public_key_size)) {
        throw std::runtime_error("Unable to size signer public key");
    }
    std::vector<unsigned char> encoded_key(public_key_size);
    if (!CryptEncodeObjectEx(X509_ASN_ENCODING, X509_PUBLIC_KEY_INFO,
                             &raw_certificate->pCertInfo->SubjectPublicKeyInfo,
                             0, nullptr, encoded_key.data(), &public_key_size)) {
        throw std::runtime_error("Unable to encode signer public key");
    }
    encoded_key.resize(public_key_size);
    if (encoded_key != signer.public_key) {
        throw std::runtime_error("APK signer public key does not match certificate");
    }
    BCRYPT_KEY_HANDLE raw_key = nullptr;
    if (!CryptImportPublicKeyInfoEx2(
            X509_ASN_ENCODING, &raw_certificate->pCertInfo->SubjectPublicKeyInfo,
            0, nullptr, &raw_key)) {
        throw std::runtime_error("Unable to import APK signer public key");
    }
    std::unique_ptr<void, KeyCloser> key(raw_key);

    const auto signed_data_hash = HashBytes(signer.signed_data.data(),
                                            signer.signed_data.size());
    BCRYPT_PKCS1_PADDING_INFO padding{BCRYPT_SHA256_ALGORITHM};
    const NTSTATUS verification = BCryptVerifySignature(
        raw_key, &padding,
        const_cast<PUCHAR>(signed_data_hash.data()),
        static_cast<ULONG>(signed_data_hash.size()),
        const_cast<PUCHAR>(signer.signature.data()),
        static_cast<ULONG>(signer.signature.size()), BCRYPT_PAD_PKCS1);
    if (!BCRYPT_SUCCESS(verification)) {
        throw std::runtime_error("APK signer signature is invalid");
    }

    const auto certificate_hash = HashBytes(signer.certificate.data(),
                                            signer.certificate.size());
    return Hex(certificate_hash.data(), certificate_hash.size());
}

}  // namespace

ApkSignatureResult VerifyApkV2Signature(const std::filesystem::path& apk_path) {
    ApkSignatureResult result;
    try {
        const auto layout = ReadLayout(apk_path);
        const auto content_digest = ComputeContentDigest(apk_path, layout);
        ByteReader v2_block(layout.v2_block);
        auto signers = v2_block.ReadLengthPrefixed();
        if (!v2_block.empty()) {
            throw std::runtime_error("Unexpected trailing bytes in APK v2 block");
        }
        std::set<std::wstring> signer_set;
        while (!signers.empty()) {
            const auto signer = ParseSigner(signers.ReadLengthPrefixed());
            signer_set.insert(VerifySigner(signer, content_digest));
        }
        if (signer_set.empty()) {
            throw std::runtime_error("APK v2 signing block contains no signers");
        }
        result.signer_sha256.assign(signer_set.begin(), signer_set.end());
        result.ok = true;
        result.message = L"APK v2 signature and signed content digest verified.";
    } catch (const std::exception& error) {
        const int needed = MultiByteToWideChar(CP_UTF8, 0, error.what(), -1,
                                               nullptr, 0);
        std::wstring detail;
        if (needed > 1) {
            detail.resize(static_cast<std::size_t>(needed));
            MultiByteToWideChar(CP_UTF8, 0, error.what(), -1,
                                detail.data(), needed);
            detail.resize(static_cast<std::size_t>(needed - 1));
        }
        result.message = L"APK v2 verification failed: " + detail;
    }
    return result;
}

}  // namespace astra
