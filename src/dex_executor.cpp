#include "astra/dex_executor.hpp"

#include "astra/dex_catalog.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace astra {
namespace {

constexpr std::uint64_t kMaximumDexBytes = 256ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t kMaximumRegisters = 4096;
constexpr std::uint32_t kMaximumCodeUnits = 1000000;
constexpr std::uint64_t kMaximumExecutionSteps = 1000000;
constexpr std::size_t kMaximumStringBytes = 4U * 1024U * 1024U;

std::uint32_t Bits(std::int32_t value) {
    return std::bit_cast<std::uint32_t>(value);
}

std::int32_t SignedBits(std::uint32_t value) {
    return std::bit_cast<std::int32_t>(value);
}

std::int32_t Sign8(std::uint8_t value) {
    return value < 0x80U ? static_cast<std::int32_t>(value) :
                           static_cast<std::int32_t>(value) - 0x100;
}

std::int32_t Sign16(std::uint16_t value) {
    return value < 0x8000U ? static_cast<std::int32_t>(value) :
                             static_cast<std::int32_t>(value) - 0x10000;
}

std::wstring WideAscii(const std::string& value) {
    std::wstring result;
    result.reserve(value.size());
    for (const unsigned char character : value) {
        result.push_back(static_cast<wchar_t>(character));
    }
    return result;
}

class DexView {
public:
    explicit DexView(const std::filesystem::path& path) {
        const DexInfo verified = ValidateDexFile(path, L"managed-execution");
        if (verified.file_size < 0x70U || verified.file_size > kMaximumDexBytes) {
            throw std::runtime_error("DEX is outside the execution size limit");
        }
        bytes_.resize(verified.file_size);
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            throw std::runtime_error("Unable to open verified DEX for execution");
        }
        input.read(reinterpret_cast<char*>(bytes_.data()),
                   static_cast<std::streamsize>(bytes_.size()));
        if (input.gcount() != static_cast<std::streamsize>(bytes_.size())) {
            throw std::runtime_error("Verified DEX became truncated during read");
        }
    }

    std::size_t size() const noexcept {
        return bytes_.size();
    }

    void Check(std::uint64_t offset, std::uint64_t amount,
               const char* name) const {
        if (offset > bytes_.size() || amount > bytes_.size() - offset) {
            throw std::runtime_error(std::string(name) + " is out of DEX range");
        }
    }

    std::uint16_t U16(std::size_t offset) const {
        Check(offset, 2, "16-bit read");
        return static_cast<std::uint16_t>(bytes_[offset]) |
               (static_cast<std::uint16_t>(bytes_[offset + 1]) << 8U);
    }

    std::uint32_t U32(std::size_t offset) const {
        Check(offset, 4, "32-bit read");
        return static_cast<std::uint32_t>(bytes_[offset]) |
               (static_cast<std::uint32_t>(bytes_[offset + 1]) << 8U) |
               (static_cast<std::uint32_t>(bytes_[offset + 2]) << 16U) |
               (static_cast<std::uint32_t>(bytes_[offset + 3]) << 24U);
    }

    std::uint32_t Uleb128(std::size_t& position) const {
        std::uint32_t result = 0;
        for (std::uint32_t index = 0; index < 5; ++index) {
            Check(position, 1, "ULEB128");
            const std::uint8_t byte = bytes_[position++];
            if (index == 4 && (byte & 0xf0U) != 0) {
                throw std::runtime_error("ULEB128 value overflows 32 bits");
            }
            result |= static_cast<std::uint32_t>(byte & 0x7fU) <<
                      (index * 7U);
            if ((byte & 0x80U) == 0) {
                return result;
            }
        }
        throw std::runtime_error("ULEB128 value is too long");
    }

    std::string String(std::uint32_t index) const {
        const std::uint32_t count = U32(0x38);
        const std::uint32_t table = U32(0x3c);
        if (index >= count) {
            throw std::runtime_error("DEX string index is invalid");
        }
        std::size_t position = U32(
            table + static_cast<std::size_t>(index) * 4U);
        const std::uint32_t utf16_length = Uleb128(position);
        if (utf16_length > kMaximumStringBytes) {
            throw std::runtime_error("DEX string character count is excessive");
        }
        const std::size_t begin = position;
        while (position < bytes_.size() && bytes_[position] != 0 &&
               position - begin <= kMaximumStringBytes) {
            ++position;
        }
        if (position >= bytes_.size() ||
            position - begin > kMaximumStringBytes) {
            throw std::runtime_error("DEX string is unterminated or excessive");
        }
        return std::string(
            reinterpret_cast<const char*>(bytes_.data() + begin),
            position - begin);
    }

    std::string TypeDescriptor(std::uint32_t index) const {
        const std::uint32_t count = U32(0x40);
        const std::uint32_t table = U32(0x44);
        if (index >= count) {
            throw std::runtime_error("DEX type index is invalid");
        }
        return String(U32(table + static_cast<std::size_t>(index) * 4U));
    }

private:
    std::vector<unsigned char> bytes_;
};

struct ResolvedMethod {
    std::uint32_t method_index{};
    std::uint32_t code_offset{};
};

std::uint32_t FindType(const DexView& dex, const std::string& descriptor) {
    const std::uint32_t count = dex.U32(0x40);
    for (std::uint32_t index = 0; index < count; ++index) {
        if (dex.TypeDescriptor(index) == descriptor) {
            return index;
        }
    }
    throw std::runtime_error("Requested class descriptor is absent from DEX");
}

bool IsStaticNoArgumentIntPrototype(const DexView& dex,
                                    std::uint32_t prototype_index) {
    const std::uint32_t count = dex.U32(0x48);
    const std::uint32_t table = dex.U32(0x4c);
    if (prototype_index >= count) {
        throw std::runtime_error("DEX prototype index is invalid");
    }
    const std::size_t item =
        table + static_cast<std::size_t>(prototype_index) * 12U;
    if (dex.TypeDescriptor(dex.U32(item + 4)) != "I") {
        return false;
    }
    const std::uint32_t parameters_offset = dex.U32(item + 8);
    if (parameters_offset == 0) {
        return true;
    }
    const std::uint32_t parameter_count = dex.U32(parameters_offset);
    dex.Check(static_cast<std::uint64_t>(parameters_offset) + 4ULL,
              static_cast<std::uint64_t>(parameter_count) * 2ULL,
              "DEX prototype parameter list");
    return parameter_count == 0;
}

std::uint32_t FindMethodIndex(const DexView& dex, std::uint32_t class_index,
                              const std::string& method_name) {
    const std::uint32_t count = dex.U32(0x58);
    const std::uint32_t table = dex.U32(0x5c);
    std::uint32_t selected = std::numeric_limits<std::uint32_t>::max();
    for (std::uint32_t index = 0; index < count; ++index) {
        const std::size_t item = table + static_cast<std::size_t>(index) * 8U;
        if (dex.U16(item) != class_index ||
            dex.String(dex.U32(item + 4)) != method_name ||
            !IsStaticNoArgumentIntPrototype(dex, dex.U16(item + 2))) {
            continue;
        }
        if (selected != std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("Requested DEX method is ambiguous");
        }
        selected = index;
    }
    if (selected == std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("No matching static ()I DEX method was found");
    }
    return selected;
}

std::size_t FindClassData(const DexView& dex, std::uint32_t class_index) {
    const std::uint32_t count = dex.U32(0x60);
    const std::uint32_t table = dex.U32(0x64);
    for (std::uint32_t index = 0; index < count; ++index) {
        const std::size_t item = table + static_cast<std::size_t>(index) * 32U;
        if (dex.U32(item) == class_index) {
            const std::uint32_t class_data = dex.U32(item + 24);
            if (class_data == 0) {
                throw std::runtime_error("Requested DEX class has no class data");
            }
            dex.Check(class_data, 1, "DEX class data");
            return class_data;
        }
    }
    throw std::runtime_error("Requested class is referenced but not defined");
}

void SkipEncodedFields(const DexView& dex, std::size_t& position,
                       std::uint32_t count) {
    std::uint32_t current = 0;
    for (std::uint32_t index = 0; index < count; ++index) {
        const std::uint32_t difference = dex.Uleb128(position);
        if (index != 0 && difference == 0) {
            throw std::runtime_error("DEX encoded field indices are not increasing");
        }
        if (difference > std::numeric_limits<std::uint32_t>::max() - current) {
            throw std::runtime_error("DEX encoded field index overflows");
        }
        current += difference;
        (void)dex.Uleb128(position);
    }
}

void ScanEncodedMethods(const DexView& dex, std::size_t& position,
                        std::uint32_t count, bool direct,
                        std::uint32_t target, ResolvedMethod& resolved,
                        bool& found) {
    const std::uint32_t method_count = dex.U32(0x58);
    std::uint32_t current = 0;
    for (std::uint32_t index = 0; index < count; ++index) {
        const std::uint32_t difference = dex.Uleb128(position);
        if (index != 0 && difference == 0) {
            throw std::runtime_error("DEX encoded method indices are not increasing");
        }
        if (difference > std::numeric_limits<std::uint32_t>::max() - current) {
            throw std::runtime_error("DEX encoded method index overflows");
        }
        current += difference;
        const std::uint32_t access_flags = dex.Uleb128(position);
        const std::uint32_t code_offset = dex.Uleb128(position);
        if (current >= method_count) {
            throw std::runtime_error("DEX encoded method index is invalid");
        }
        if (current != target) {
            continue;
        }
        if (found) {
            throw std::runtime_error("DEX method has duplicate definitions");
        }
        if (!direct || (access_flags & 0x0008U) == 0) {
            throw std::runtime_error("Requested DEX method is not static/direct");
        }
        if ((access_flags & (0x0100U | 0x0400U)) != 0 || code_offset == 0) {
            throw std::runtime_error("Requested DEX method has no executable code");
        }
        resolved = {current, code_offset};
        found = true;
    }
}

ResolvedMethod ResolveMethod(const DexView& dex,
                             const std::string& class_descriptor,
                             const std::string& method_name) {
    const std::uint32_t class_index = FindType(dex, class_descriptor);
    const std::uint32_t method_index =
        FindMethodIndex(dex, class_index, method_name);
    std::size_t position = FindClassData(dex, class_index);
    const std::uint32_t static_fields = dex.Uleb128(position);
    const std::uint32_t instance_fields = dex.Uleb128(position);
    const std::uint32_t direct_methods = dex.Uleb128(position);
    const std::uint32_t virtual_methods = dex.Uleb128(position);
    if (static_fields > dex.U32(0x50) || instance_fields > dex.U32(0x50) ||
        direct_methods > dex.U32(0x58) || virtual_methods > dex.U32(0x58)) {
        throw std::runtime_error("DEX class data counts exceed identifier tables");
    }
    SkipEncodedFields(dex, position, static_fields);
    SkipEncodedFields(dex, position, instance_fields);
    ResolvedMethod resolved;
    bool found = false;
    ScanEncodedMethods(dex, position, direct_methods, true, method_index,
                       resolved, found);
    ScanEncodedMethods(dex, position, virtual_methods, false, method_index,
                       resolved, found);
    if (!found) {
        throw std::runtime_error("Requested DEX method has no class definition");
    }
    return resolved;
}

std::uint8_t InstructionWidth(std::uint16_t first) {
    const std::uint8_t opcode = static_cast<std::uint8_t>(first & 0xffU);
    if (opcode == 0x00U) {
        if ((first >> 8U) != 0) {
            throw std::runtime_error("DEX payload pseudo-instructions are unsupported");
        }
        return 1;
    }
    if (opcode == 0x01U || opcode == 0x0fU || opcode == 0x12U ||
        opcode == 0x28U || (opcode >= 0x7bU && opcode <= 0x7cU) ||
        (opcode >= 0xb0U && opcode <= 0xbaU)) {
        return 1;
    }
    if (opcode == 0x02U || opcode == 0x13U || opcode == 0x15U ||
        opcode == 0x29U || (opcode >= 0x32U && opcode <= 0x3dU) ||
        (opcode >= 0x90U && opcode <= 0x9aU) ||
        (opcode >= 0xd0U && opcode <= 0xe2U)) {
        return 2;
    }
    if (opcode == 0x03U || opcode == 0x14U || opcode == 0x2aU) {
        return 3;
    }
    throw std::runtime_error("DEX opcode is outside the managed-proof subset");
}

std::int32_t ArithmeticShiftRight(std::int32_t value, std::uint32_t distance) {
    distance &= 31U;
    if (distance == 0) {
        return value;
    }
    std::uint32_t result = Bits(value) >> distance;
    if (value < 0) {
        result |= 0xffffffffU << (32U - distance);
    }
    return SignedBits(result);
}

std::int32_t IntBinary(std::uint8_t operation, std::int32_t left,
                       std::int32_t right) {
    switch (operation) {
        case 0:
            return SignedBits(Bits(left) + Bits(right));
        case 1:
            return SignedBits(Bits(left) - Bits(right));
        case 2:
            return SignedBits(Bits(left) * Bits(right));
        case 3:
            if (right == 0) {
                throw std::runtime_error("DEX integer division by zero");
            }
            if (left == std::numeric_limits<std::int32_t>::min() &&
                right == -1) {
                return left;
            }
            return left / right;
        case 4:
            if (right == 0) {
                throw std::runtime_error("DEX integer remainder by zero");
            }
            if (left == std::numeric_limits<std::int32_t>::min() &&
                right == -1) {
                return 0;
            }
            return left % right;
        case 5:
            return SignedBits(Bits(left) & Bits(right));
        case 6:
            return SignedBits(Bits(left) | Bits(right));
        case 7:
            return SignedBits(Bits(left) ^ Bits(right));
        case 8:
            return SignedBits(Bits(left) << (Bits(right) & 31U));
        case 9:
            return ArithmeticShiftRight(left, Bits(right));
        case 10:
            return SignedBits(Bits(left) >> (Bits(right) & 31U));
        default:
            throw std::runtime_error("DEX integer operation is unsupported");
    }
}

std::int32_t& Register(std::vector<std::int32_t>& registers,
                       std::uint32_t index) {
    if (index >= registers.size()) {
        throw std::runtime_error("DEX instruction references an invalid register");
    }
    return registers[index];
}

bool Compare(std::uint8_t opcode, std::int32_t left, std::int32_t right) {
    switch (opcode) {
        case 0x32U: return left == right;
        case 0x33U: return left != right;
        case 0x34U: return left < right;
        case 0x35U: return left >= right;
        case 0x36U: return left > right;
        case 0x37U: return left <= right;
        case 0x38U: return left == 0;
        case 0x39U: return left != 0;
        case 0x3aU: return left < 0;
        case 0x3bU: return left >= 0;
        case 0x3cU: return left > 0;
        case 0x3dU: return left <= 0;
        default: throw std::runtime_error("DEX comparison opcode is invalid");
    }
}

std::size_t BranchTarget(std::size_t pc, std::int32_t offset,
                         const std::vector<std::uint8_t>& widths,
                         bool zero_allowed) {
    if (offset == 0 && !zero_allowed) {
        throw std::runtime_error("DEX branch uses a forbidden zero offset");
    }
    const std::int64_t target = static_cast<std::int64_t>(pc) + offset;
    if (target < 0 || target >= static_cast<std::int64_t>(widths.size()) ||
        widths[static_cast<std::size_t>(target)] == 0) {
        throw std::runtime_error("DEX branch target is not an instruction boundary");
    }
    return static_cast<std::size_t>(target);
}

DexExecutionResult ExecuteCode(const DexView& dex, std::uint32_t code_offset,
                               const std::string& class_descriptor,
                               const std::string& method_name) {
    if ((code_offset & 3U) != 0) {
        throw std::runtime_error("DEX code_item is not four-byte aligned");
    }
    dex.Check(code_offset, 16, "DEX code_item header");
    const std::uint16_t register_count = dex.U16(code_offset);
    const std::uint16_t incoming_words = dex.U16(code_offset + 2U);
    const std::uint16_t outgoing_words = dex.U16(code_offset + 4U);
    const std::uint16_t try_count = dex.U16(code_offset + 6U);
    const std::uint32_t instruction_count = dex.U32(code_offset + 12U);
    if (register_count == 0 || register_count > kMaximumRegisters) {
        throw std::runtime_error("DEX code_item register count is unsupported");
    }
    if (incoming_words != 0 || outgoing_words != 0 || try_count != 0) {
        throw std::runtime_error(
            "DEX proof method must have no arguments, invokes, or try blocks");
    }
    if (instruction_count == 0 || instruction_count > kMaximumCodeUnits) {
        throw std::runtime_error("DEX instruction count is outside the proof limit");
    }
    const std::uint64_t instructions_offset =
        static_cast<std::uint64_t>(code_offset) + 16ULL;
    dex.Check(instructions_offset,
              static_cast<std::uint64_t>(instruction_count) * 2ULL,
              "DEX instructions");
    std::vector<std::uint16_t> instructions(instruction_count);
    for (std::uint32_t index = 0; index < instruction_count; ++index) {
        instructions[index] = dex.U16(
            static_cast<std::size_t>(instructions_offset) + index * 2U);
    }

    std::vector<std::uint8_t> widths(instructions.size(), 0);
    for (std::size_t pc = 0; pc < instructions.size();) {
        const std::uint8_t width = InstructionWidth(instructions[pc]);
        if (width > instructions.size() - pc) {
            throw std::runtime_error("DEX instruction is truncated");
        }
        widths[pc] = width;
        pc += width;
    }

    std::vector<std::int32_t> registers(register_count, 0);
    std::size_t pc = 0;
    std::uint64_t executed = 0;
    const std::uint64_t step_limit = std::min<std::uint64_t>(
        kMaximumExecutionSteps,
        std::max<std::uint64_t>(1000, instructions.size() * 1024ULL));
    while (executed < step_limit) {
        ++executed;
        const std::uint16_t first = instructions[pc];
        const std::uint8_t opcode = static_cast<std::uint8_t>(first & 0xffU);
        const std::uint8_t high = static_cast<std::uint8_t>(first >> 8U);
        const std::uint8_t width = widths[pc];
        if (opcode == 0x00U) {
            pc += width;
        } else if (opcode == 0x01U) {
            const std::uint32_t destination = (first >> 8U) & 0x0fU;
            const std::uint32_t source = (first >> 12U) & 0x0fU;
            Register(registers, destination) = Register(registers, source);
            pc += width;
        } else if (opcode == 0x02U) {
            Register(registers, high) = Register(registers, instructions[pc + 1]);
            pc += width;
        } else if (opcode == 0x03U) {
            Register(registers, instructions[pc + 1]) =
                Register(registers, instructions[pc + 2]);
            pc += width;
        } else if (opcode == 0x0fU) {
            DexExecutionResult result;
            result.ok = true;
            result.value = Register(registers, high);
            result.instructions_executed = executed;
            result.message = L"Executed DEX " + WideAscii(class_descriptor) +
                             L"->" + WideAscii(method_name) + L"()I = " +
                             std::to_wstring(result.value) + L" (" +
                             std::to_wstring(executed) + L" instruction(s)).";
            return result;
        } else if (opcode == 0x12U) {
            const std::uint32_t destination = (first >> 8U) & 0x0fU;
            const std::uint8_t literal = static_cast<std::uint8_t>(first >> 12U);
            Register(registers, destination) =
                literal < 8U ? literal : static_cast<std::int32_t>(literal) - 16;
            pc += width;
        } else if (opcode == 0x13U) {
            Register(registers, high) = Sign16(instructions[pc + 1]);
            pc += width;
        } else if (opcode == 0x14U) {
            Register(registers, high) = SignedBits(
                static_cast<std::uint32_t>(instructions[pc + 1]) |
                (static_cast<std::uint32_t>(instructions[pc + 2]) << 16U));
            pc += width;
        } else if (opcode == 0x15U) {
            Register(registers, high) = SignedBits(
                static_cast<std::uint32_t>(instructions[pc + 1]) << 16U);
            pc += width;
        } else if (opcode == 0x28U) {
            pc = BranchTarget(pc, Sign8(high), widths, false);
        } else if (opcode == 0x29U) {
            pc = BranchTarget(pc, Sign16(instructions[pc + 1]), widths, false);
        } else if (opcode == 0x2aU) {
            const std::int32_t offset = SignedBits(
                static_cast<std::uint32_t>(instructions[pc + 1]) |
                (static_cast<std::uint32_t>(instructions[pc + 2]) << 16U));
            pc = BranchTarget(pc, offset, widths, true);
        } else if (opcode >= 0x32U && opcode <= 0x37U) {
            const std::uint32_t left_index = (first >> 8U) & 0x0fU;
            const std::uint32_t right_index = (first >> 12U) & 0x0fU;
            if (Compare(opcode, Register(registers, left_index),
                        Register(registers, right_index))) {
                pc = BranchTarget(pc, Sign16(instructions[pc + 1]), widths,
                                  false);
            } else {
                pc += width;
            }
        } else if (opcode >= 0x38U && opcode <= 0x3dU) {
            if (Compare(opcode, Register(registers, high), 0)) {
                pc = BranchTarget(pc, Sign16(instructions[pc + 1]), widths,
                                  false);
            } else {
                pc += width;
            }
        } else if (opcode >= 0x7bU && opcode <= 0x7cU) {
            const std::uint32_t destination = (first >> 8U) & 0x0fU;
            const std::uint32_t source = (first >> 12U) & 0x0fU;
            const std::int32_t value = Register(registers, source);
            Register(registers, destination) = opcode == 0x7bU ?
                SignedBits(0U - Bits(value)) : SignedBits(~Bits(value));
            pc += width;
        } else if (opcode >= 0x90U && opcode <= 0x9aU) {
            const std::uint16_t second = instructions[pc + 1];
            Register(registers, high) = IntBinary(
                static_cast<std::uint8_t>(opcode - 0x90U),
                Register(registers, second & 0xffU),
                Register(registers, second >> 8U));
            pc += width;
        } else if (opcode >= 0xb0U && opcode <= 0xbaU) {
            const std::uint32_t destination = (first >> 8U) & 0x0fU;
            const std::uint32_t source = (first >> 12U) & 0x0fU;
            Register(registers, destination) = IntBinary(
                static_cast<std::uint8_t>(opcode - 0xb0U),
                Register(registers, destination), Register(registers, source));
            pc += width;
        } else if (opcode >= 0xd0U && opcode <= 0xd7U) {
            const std::uint32_t destination = (first >> 8U) & 0x0fU;
            const std::uint32_t source = (first >> 12U) & 0x0fU;
            const std::int32_t literal = Sign16(instructions[pc + 1]);
            if (opcode == 0xd1U) {
                Register(registers, destination) = IntBinary(
                    1, literal, Register(registers, source));
            } else {
                const std::uint8_t operation = opcode == 0xd0U ? 0U :
                    static_cast<std::uint8_t>(opcode - 0xd0U);
                Register(registers, destination) = IntBinary(
                    operation, Register(registers, source), literal);
            }
            pc += width;
        } else if (opcode >= 0xd8U && opcode <= 0xe2U) {
            const std::uint16_t second = instructions[pc + 1];
            const std::uint32_t source = second & 0xffU;
            const std::int32_t literal = Sign8(
                static_cast<std::uint8_t>(second >> 8U));
            if (opcode == 0xd9U) {
                Register(registers, high) = IntBinary(
                    1, literal, Register(registers, source));
            } else {
                const std::uint8_t operation = opcode == 0xd8U ? 0U :
                    static_cast<std::uint8_t>(opcode - 0xd8U);
                Register(registers, high) = IntBinary(
                    operation, Register(registers, source), literal);
            }
            pc += width;
        } else {
            throw std::runtime_error("DEX opcode escaped predecode validation");
        }
        if (pc >= instructions.size() || widths[pc] == 0) {
            throw std::runtime_error("DEX execution fell outside instruction boundaries");
        }
    }
    throw std::runtime_error("DEX execution step limit was reached");
}

}  // namespace

DexExecutionResult ExecuteStaticIntMethod(
    const std::filesystem::path& dex_path,
    const std::string& class_descriptor,
    const std::string& method_name) {
    DexExecutionResult result;
    try {
        if (class_descriptor.empty() || method_name.empty()) {
            throw std::runtime_error("DEX class descriptor and method are required");
        }
        DexView dex(dex_path);
        const ResolvedMethod method =
            ResolveMethod(dex, class_descriptor, method_name);
        return ExecuteCode(dex, method.code_offset, class_descriptor, method_name);
    } catch (const std::exception& error) {
        result.message = L"DEX execution failed: " + WideAscii(error.what());
        return result;
    }
}

}  // namespace astra
