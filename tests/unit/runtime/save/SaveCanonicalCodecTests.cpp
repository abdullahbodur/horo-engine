#include "Horo/Runtime/Save/SaveCanonicalCodec.h"

#include <algorithm>
#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace {
    using namespace Horo;
    using namespace Horo::Runtime;

    CanonicalFieldId Field(const std::uint32_t value) {
        return CanonicalFieldId::Create(value).Value();
    }

    std::vector<std::byte> EncodeUInt32(const std::uint32_t value) {
        CanonicalValueWriter writer;
        REQUIRE(writer.WriteUInt32(value).HasValue());
        return std::move(writer).TakeBytes();
    }

    TEST_CASE("Canonical scalar codec has byte-exact little-endian golden output", "[runtime][save][canonical-codec]") {
        CanonicalValueWriter writer;
        REQUIRE(writer.WriteBool(true).HasValue());
        REQUIRE(writer.WriteUInt16(0x1234U).HasValue());
        REQUIRE(writer.WriteInt32(-2).HasValue());
        REQUIRE(writer.WriteFloat32(-0.0F).HasValue());
        REQUIRE(writer.WriteUtf8("Horo \xF0\x9F\x8C\x8D").HasValue());

        const std::vector<std::byte> expected{std::byte{0x01}, std::byte{0x34}, std::byte{0x12}, std::byte{0xfe}, std::byte{0xff},
                                              std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
                                              std::byte{0x00}, std::byte{0x09}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
                                              std::byte{'H'},  std::byte{'o'},  std::byte{'r'},  std::byte{'o'},  std::byte{' '},
                                              std::byte{0xf0}, std::byte{0x9f}, std::byte{0x8c}, std::byte{0x8d}};
        CHECK(std::ranges::equal(writer.Bytes(), expected));

        CanonicalValueReader reader{writer.Bytes()};
        CHECK(reader.ReadBool().Value());
        CHECK(reader.ReadUInt16().Value() == 0x1234U);
        CHECK(reader.ReadInt32().Value() == -2);
        CHECK(std::bit_cast<std::uint32_t>(reader.ReadFloat32().Value()) == 0U);
        CHECK(reader.ReadUtf8().Value() == "Horo \xF0\x9F\x8C\x8D");
        CHECK(reader.RequireFinished().HasValue());
    }

    TEST_CASE("Canonical math optional variant and sequence framing round trip", "[runtime][save][canonical-codec]") {
        CanonicalValueWriter writer;
        REQUIRE(writer.WriteVec3({1.0F, -2.0F, 3.5F}).HasValue());
        REQUIRE(writer.WriteQuaternion({0.0F, 0.5F, 0.0F, 0.5F}).HasValue());
        REQUIRE(writer.WriteOptionalPresence(true).HasValue());
        REQUIRE(writer.WriteVariantIndex(2).HasValue());
        REQUIRE(writer.WriteSequenceSize(3).HasValue());

        CanonicalValueReader reader{writer.Bytes()};
        const Math::Vec3 expectedVector{1.0F, -2.0F, 3.5F};
        const Math::Quaternion expectedRotation{0.0F, 0.5F, 0.0F, 0.5F};
        CHECK(reader.ReadVec3().Value() == expectedVector);
        CHECK(reader.ReadQuaternion().Value() == expectedRotation);
        CHECK(reader.ReadOptionalPresence().Value());
        CHECK(reader.ReadVariantIndex(3).Value() == 2);
        CHECK(reader.ReadSequenceSize().Value() == 3);
        CHECK(reader.RequireFinished().HasValue());
    }

    TEST_CASE("Canonical fixed-width integers preserve edge values", "[runtime][save][canonical-codec]") {
        CanonicalValueWriter writer;
        REQUIRE(writer.WriteUInt8(std::numeric_limits<std::uint8_t>::max()).HasValue());
        REQUIRE(writer.WriteUInt16(std::numeric_limits<std::uint16_t>::max()).HasValue());
        REQUIRE(writer.WriteUInt32(std::numeric_limits<std::uint32_t>::max()).HasValue());
        REQUIRE(writer.WriteUInt64(std::numeric_limits<std::uint64_t>::max()).HasValue());
        REQUIRE(writer.WriteInt8(std::numeric_limits<std::int8_t>::min()).HasValue());
        REQUIRE(writer.WriteInt16(std::numeric_limits<std::int16_t>::min()).HasValue());
        REQUIRE(writer.WriteInt32(std::numeric_limits<std::int32_t>::min()).HasValue());
        REQUIRE(writer.WriteInt64(std::numeric_limits<std::int64_t>::min()).HasValue());

        CanonicalValueReader reader{writer.Bytes()};
        CHECK(reader.ReadUInt8().Value() == std::numeric_limits<std::uint8_t>::max());
        CHECK(reader.ReadUInt16().Value() == std::numeric_limits<std::uint16_t>::max());
        CHECK(reader.ReadUInt32().Value() == std::numeric_limits<std::uint32_t>::max());
        CHECK(reader.ReadUInt64().Value() == std::numeric_limits<std::uint64_t>::max());
        CHECK(reader.ReadInt8().Value() == std::numeric_limits<std::int8_t>::min());
        CHECK(reader.ReadInt16().Value() == std::numeric_limits<std::int16_t>::min());
        CHECK(reader.ReadInt32().Value() == std::numeric_limits<std::int32_t>::min());
        CHECK(reader.ReadInt64().Value() == std::numeric_limits<std::int64_t>::min());
        CHECK(reader.RequireFinished().HasValue());
    }

    TEST_CASE("Canonical maps sets and records ignore input ordering", "[runtime][save][canonical-codec]") {
        const std::vector<CanonicalMapEntry> mapA{{EncodeUInt32(9), EncodeUInt32(90)}, {EncodeUInt32(1), EncodeUInt32(10)}};
        const std::vector<CanonicalMapEntry> mapB{mapA[1], mapA[0]};
        CanonicalValueWriter first;
        CanonicalValueWriter second;
        REQUIRE(first.WriteMap(mapA).HasValue());
        REQUIRE(second.WriteMap(mapB).HasValue());
        CHECK(std::ranges::equal(first.Bytes(), second.Bytes()));

        const std::vector<std::vector<std::byte>> setA{EncodeUInt32(8), EncodeUInt32(2)};
        const std::vector<std::vector<std::byte>> setB{setA.rbegin(), setA.rend()};
        CanonicalValueWriter setFirst;
        CanonicalValueWriter setSecond;
        REQUIRE(setFirst.WriteSet(setA).HasValue());
        REQUIRE(setSecond.WriteSet(setB).HasValue());
        CHECK(std::ranges::equal(setFirst.Bytes(), setSecond.Bytes()));

        const std::vector<CanonicalRecordField> recordA{{Field(9), EncodeUInt32(90)}, {Field(1), EncodeUInt32(10)}};
        const std::vector<CanonicalRecordField> recordB{recordA[1], recordA[0]};
        CanonicalValueWriter recordFirst;
        CanonicalValueWriter recordSecond;
        REQUIRE(recordFirst.WriteRecord(recordA).HasValue());
        REQUIRE(recordSecond.WriteRecord(recordB).HasValue());
        CHECK(std::ranges::equal(recordFirst.Bytes(), recordSecond.Bytes()));

        CanonicalValueReader reader{recordFirst.Bytes()};
        const auto decoded = reader.ReadRecord();
        REQUIRE(decoded.HasValue());
        REQUIRE(decoded.Value().size() == 2);
        CHECK(decoded.Value()[0].id == Field(1));
        CHECK(decoded.Value()[1].id == Field(9));
        CHECK(reader.RequireFinished().HasValue());
    }

    TEST_CASE("Canonical codec rejects duplicate canonical collection identities", "[runtime][save][canonical-codec]") {
        const auto key = EncodeUInt32(1);
        const std::vector<CanonicalMapEntry> duplicateMap{{key, EncodeUInt32(10)}, {key, EncodeUInt32(11)}};
        CanonicalValueWriter writer;
        CHECK(writer.WriteMap(duplicateMap).HasError());

        const std::vector<std::vector<std::byte>> duplicateSet{key, key};
        CHECK(CanonicalValueWriter{}.WriteSet(duplicateSet).HasError());

        const std::vector<CanonicalRecordField> duplicateRecord{{Field(1), key}, {Field(1), EncodeUInt32(2)}};
        CHECK(CanonicalValueWriter{}.WriteRecord(duplicateRecord).HasError());

        CanonicalValueWriter canonicalMap;
        REQUIRE(canonicalMap.WriteMap({duplicateMap.data(), 1}).HasValue());
        auto malformedMap = std::move(canonicalMap).TakeBytes();
        const std::vector<std::byte> duplicateEntry{malformedMap.begin() + 4, malformedMap.end()};
        malformedMap.insert(malformedMap.end(), duplicateEntry.begin(), duplicateEntry.end());
        malformedMap[0] = std::byte{2};
        CHECK(CanonicalValueReader{malformedMap}.ReadMap().HasError());
    }

    TEST_CASE("Canonical codec rejects non-finite malformed UTF-8 and noncanonical input", "[runtime][save][canonical-codec]") {
        CanonicalValueWriter writer;
        CHECK(writer.WriteFloat32(std::numeric_limits<float>::quiet_NaN()).HasError());
        CHECK(writer.WriteFloat64(std::numeric_limits<double>::infinity()).HasError());
        const std::string malformed{"\xc0\x80", 2};
        CHECK(writer.WriteUtf8(malformed).HasError());

        const std::array invalidBool{std::byte{2}};
        CHECK(CanonicalValueReader{invalidBool}.ReadBool().HasError());
        const std::array negativeZero{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0x80}};
        CHECK(CanonicalValueReader{negativeZero}.ReadFloat32().HasError());
        const std::array truncatedString{std::byte{2}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{'a'}};
        CHECK(CanonicalValueReader{truncatedString}.ReadUtf8().HasError());
        const std::array trailing{std::byte{1}, std::byte{0}};
        CanonicalValueReader trailingReader{trailing};
        REQUIRE(trailingReader.ReadBool().HasValue());
        CHECK(trailingReader.RequireFinished().HasError());
    }

    TEST_CASE("Canonical codec reports stable nested field context and enforces limits", "[runtime][save][canonical-codec]") {
        auto outer = CanonicalValueWriter{}.ForField(Field(3));
        auto inner = outer.ForField(Field(7));
        const auto failure = inner.WriteFloat32(std::numeric_limits<float>::quiet_NaN());
        REQUIRE(failure.HasError());
        REQUIRE(failure.ErrorValue().context.fieldPath.size() == 2);
        CHECK(failure.ErrorValue().context.fieldPath[0] == Field(3));
        CHECK(failure.ErrorValue().context.fieldPath[1] == Field(7));
        CHECK(failure.ErrorValue().context.byteOffset == 0);

        CanonicalCodecLimits limits;
        limits.maximumBytes = 4;
        limits.maximumStringBytes = 4;
        CanonicalValueWriter bounded{limits};
        CHECK(bounded.WriteUInt64(1).HasError());
        const std::array oversized{std::byte{}, std::byte{}, std::byte{}, std::byte{}, std::byte{}};
        CHECK(CanonicalValueReader{oversized, limits}.ReadUInt8().HasError());
    }
}  // namespace
