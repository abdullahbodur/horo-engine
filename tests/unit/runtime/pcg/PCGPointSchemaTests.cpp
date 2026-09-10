#include "Horo/PCG/PCGErrors.h"
#include "Horo/PCG/PCGPointSchema.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <set>

namespace Horo::PCG {
    namespace {
        PCGAttributeKey Key(const std::string_view value) {
            auto key = PCGAttributeKey::Create(value);
            REQUIRE(key.HasValue());
            return std::move(key).Value();
        }

        std::shared_ptr<const PCGPointSchema> Schema(std::vector<PCGAttributeDescriptor> fields,
                                                     const PCGOperationalTier tier = PCGOperationalTier::Baseline) {
            auto schema = PCGPointSchema::Capture({tier, fields});
            REQUIRE(schema.HasValue());
            return std::make_shared<const PCGPointSchema>(std::move(schema).Value());
        }

        PCGPointStorageCandidate Candidate(std::shared_ptr<const PCGPointSchema> schema, const std::size_t count = 2) {
            PCGPointStorageCandidate candidate{std::move(schema)};
            candidate.core.transforms.resize(count);
            candidate.core.bounds.resize(count, {{-1.0F, -1.0F, -1.0F}, {1.0F, 1.0F, 1.0F}});
            candidate.core.densities.resize(count, 0.5F);
            candidate.core.seeds.resize(count);
            for (std::size_t index = 0; index < count; ++index)
                candidate.core.seeds[index] = 100 + index;
            for (const auto &field : candidate.schema->Attributes()) {
                PCGAttributeColumn column{std::string(field.key.Value())};
                switch (field.type) {
                    case PCGAttributeType::Boolean:
                        column.values = PCGBoolColumn(count, 1);
                        break;
                    case PCGAttributeType::SignedInteger:
                        column.values = PCGSignedIntegerColumn(count, -2);
                        break;
                    case PCGAttributeType::UnsignedInteger:
                        column.values = PCGUnsignedIntegerColumn(count, 2);
                        break;
                    case PCGAttributeType::Scalar:
                        column.values = PCGScalarColumn(count, 0.25);
                        break;
                    case PCGAttributeType::Vector2:
                        column.values = PCGVector2Column(count, {1.0F, 2.0F});
                        break;
                    case PCGAttributeType::Vector3:
                        column.values = PCGVector3Column(count, {1.0F, 2.0F, 3.0F});
                        break;
                    case PCGAttributeType::Vector4:
                        column.values = PCGVector4Column(count, {1.0F, 2.0F, 3.0F, 4.0F});
                        break;
                }
                candidate.attributes.push_back(std::move(column));
            }
            return candidate;
        }

        void CheckError(const auto &result, const ErrorCodeDescriptor &descriptor) {
            REQUIRE(result.HasError());
            const auto &error = result.ErrorValue();
            CAPTURE(error.domain.Value(), error.code.Value());
            CHECK(error.domain.Value() == "horo.pcg");
            CHECK(error.code.Value() == descriptor.code.Value());
        }
    }  // namespace

    TEST_CASE("PCG operational tiers expose exact ADR-156 point and memory limits", "[unit][pcg][point]") {
        const auto baseline = LimitsForTier(PCGOperationalTier::Baseline).Value();
        CHECK(baseline.maximumNodes == 32);
        CHECK(baseline.maximumAttributesPerPoint == 16);
        CHECK(baseline.maximumAttributeValueBytesPerPoint == 256);
        CHECK(baseline.maximumPointsPerNodeOutput == 16'384);
        CHECK(baseline.maximumMaterializedPointRecords == 65'536);
        CHECK(baseline.maximumAggregateBytes == 112 * 1024 * 1024);
        const auto standard = LimitsForTier(PCGOperationalTier::Standard).Value();
        CHECK(standard.maximumNodes == 256);
        CHECK(standard.maximumAttributesPerPoint == 32);
        CHECK(standard.maximumAttributeValueBytesPerPoint == 512);
        CHECK(standard.maximumPointsPerNodeOutput == 262'144);
        CHECK(standard.maximumMaterializedPointRecords == 1'048'576);
        CHECK(standard.maximumAggregateBytes == 1'024ULL * 1024 * 1024);
        const auto high = LimitsForTier(PCGOperationalTier::High).Value();
        CHECK(high.maximumNodes == 1'024);
        CHECK(high.maximumAttributesPerPoint == 64);
        CHECK(high.maximumAttributeValueBytesPerPoint == 1'024);
        CHECK(high.maximumPointsPerNodeOutput == 2'097'152);
        CHECK(high.maximumMaterializedPointRecords == 8'388'608);
        CHECK(high.maximumAggregateBytes == 4'096ULL * 1024 * 1024);
        CheckError(LimitsForTier(static_cast<PCGOperationalTier>(255)), PCGErrors::PointSchemaInvalid);
    }

    TEST_CASE("PCG attribute keys enforce canonical bounded namespaces", "[unit][pcg][point]") {
        CHECK(PCGAttributeKey::Create("terrain.slope_2").HasValue());
        for (const auto invalid : {"", "slope", ".slope", "terrain.", "terrain..slope", "Terrain.slope", "terrain.-slope", "terrain.2slope",
                                   "terrain.\xF0\x28\x8C\x28"}) {
            INFO(invalid);
            CHECK(PCGAttributeKey::Create(invalid).HasError());
        }
        CHECK(PCGAttributeKey::Create(std::string(MaximumAttributeKeyBytes, 'a')).HasError());
        CHECK(PCGAttributeKey::Create(std::string(MaximumAttributeKeyBytes + 1, 'a')).HasError());
        const std::string exact = "a." + std::string(MaximumAttributeKeyBytes - 2, 'b');
        CHECK(PCGAttributeKey::Create(exact).HasValue());
    }

    TEST_CASE("PCG schema captures deterministic order and exact payload boundaries", "[unit][pcg][point]") {
        std::vector<PCGAttributeDescriptor> fields;
        for (std::size_t index = 16; index > 0; --index)
            fields.push_back({Key("attr.a" + std::to_string(index)), PCGAttributeType::Vector4});
        auto schema = PCGPointSchema::Capture({PCGOperationalTier::Baseline, fields});
        REQUIRE(schema.HasValue());
        CHECK(schema.Value().Attributes().size() == 16);
        CHECK(schema.Value().CanonicalValueBytesPerPoint() == 256);
        CHECK(std::is_sorted(schema.Value().Attributes().begin(), schema.Value().Attributes().end()));
        fields.push_back({Key("attr.extra"), PCGAttributeType::Boolean});
        CheckError(PCGPointSchema::Capture({PCGOperationalTier::Baseline, fields}), PCGErrors::PointCapacityExceeded);
    }

    TEST_CASE("PCG schemas reject duplicate and unknown value types", "[unit][pcg][point]") {
        const std::vector duplicate{PCGAttributeDescriptor{Key("pcg.value"), PCGAttributeType::Scalar},
                                    PCGAttributeDescriptor{Key("pcg.value"), PCGAttributeType::Scalar}};
        CheckError(PCGPointSchema::Capture({PCGOperationalTier::Baseline, duplicate}), PCGErrors::PointAttributeDuplicate);
        const std::vector unknown{PCGAttributeDescriptor{Key("pcg.value"), static_cast<PCGAttributeType>(255)}};
        CheckError(PCGPointSchema::Capture({PCGOperationalTier::Baseline, unknown}), PCGErrors::PointSchemaInvalid);
    }

    TEST_CASE("PCG point storage validates SoA shape types and safe lookup", "[unit][pcg][point]") {
        auto schema = Schema({{Key("terrain.slope"), PCGAttributeType::Scalar}, {Key("surface.normal"), PCGAttributeType::Vector3}});
        auto captured = CapturePointStorage(Candidate(schema));
        REQUIRE(captured.HasValue());
        CHECK(captured.Value()->PointCount() == 2);
        CHECK(captured.Value()->Schema().Tier() == PCGOperationalTier::Baseline);
        CHECK(captured.Value()->Transforms().size() == 2);
        CHECK(captured.Value()->Bounds().size() == 2);
        CHECK(captured.Value()->Densities()[0] == 0.5F);
        REQUIRE(captured.Value()->FindColumn<PCGScalarColumn>("terrain.slope") != nullptr);
        CHECK(captured.Value()->FindColumn<PCGVector3Column>("terrain.slope") == nullptr);
        CHECK(captured.Value()->FindColumn<PCGScalarColumn>("terrain.missing") == nullptr);
        CHECK(captured.Value()->Seeds()[1] == 101);

        auto missing = Candidate(schema);
        missing.attributes.pop_back();
        CheckError(CapturePointStorage(std::move(missing)), PCGErrors::PointDataInvalid);
        auto unknown = Candidate(schema);
        unknown.attributes[0].key = "other.value";
        CheckError(CapturePointStorage(std::move(unknown)), PCGErrors::PointAttributeUnknown);
        auto mismatch = Candidate(schema);
        mismatch.attributes[0].values = PCGScalarColumn(2);
        CheckError(CapturePointStorage(std::move(mismatch)), PCGErrors::PointAttributeTypeMismatch);
        auto length = Candidate(schema);
        std::get<PCGVector3Column>(length.attributes[0].values).push_back({});
        CheckError(CapturePointStorage(std::move(length)), PCGErrors::PointDataInvalid);

        auto duplicate = Candidate(schema);
        duplicate.attributes[1].key = duplicate.attributes[0].key;
        CheckError(CapturePointStorage(std::move(duplicate)), PCGErrors::PointAttributeDuplicate);

        auto coreLength = Candidate(schema);
        coreLength.core.seeds.pop_back();
        CheckError(CapturePointStorage(std::move(coreLength)), PCGErrors::PointDataInvalid);
        CheckError(CapturePointStorage({}), PCGErrors::PointSchemaInvalid);
    }

    TEST_CASE("PCG point storage supports every closed typed attribute column", "[unit][pcg][point]") {
        auto schema = Schema({{Key("typed.boolean"), PCGAttributeType::Boolean},
                              {Key("typed.signed"), PCGAttributeType::SignedInteger},
                              {Key("typed.unsigned"), PCGAttributeType::UnsignedInteger},
                              {Key("typed.scalar"), PCGAttributeType::Scalar},
                              {Key("typed.vector2"), PCGAttributeType::Vector2},
                              {Key("typed.vector3"), PCGAttributeType::Vector3},
                              {Key("typed.vector4"), PCGAttributeType::Vector4}});
        auto captured = CapturePointStorage(Candidate(schema));
        REQUIRE(captured.HasValue());
        CHECK(captured.Value()->FindColumn<PCGBoolColumn>("typed.boolean") != nullptr);
        CHECK(captured.Value()->FindColumn<PCGSignedIntegerColumn>("typed.signed") != nullptr);
        CHECK(captured.Value()->FindColumn<PCGUnsignedIntegerColumn>("typed.unsigned") != nullptr);
        CHECK(captured.Value()->FindColumn<PCGScalarColumn>("typed.scalar") != nullptr);
        CHECK(captured.Value()->FindColumn<PCGVector2Column>("typed.vector2") != nullptr);
        CHECK(captured.Value()->FindColumn<PCGVector3Column>("typed.vector3") != nullptr);
        CHECK(captured.Value()->FindColumn<PCGVector4Column>("typed.vector4") != nullptr);
    }

    TEST_CASE("PCG point storage rejects invalid core and attribute values", "[unit][pcg][point]") {
        auto schema = Schema({{Key("pcg.scalar"), PCGAttributeType::Scalar}});
        auto invalidTransform = Candidate(schema);
        invalidTransform.core.transforms[0].translation.x = std::numeric_limits<float>::infinity();
        CheckError(CapturePointStorage(std::move(invalidTransform)), PCGErrors::PointDataInvalid);
        auto invalidBounds = Candidate(schema);
        invalidBounds.core.bounds[0].minimum.x = 2.0F;
        CheckError(CapturePointStorage(std::move(invalidBounds)), PCGErrors::PointDataInvalid);
        for (const float density : {-0.01F, 1.01F, std::numeric_limits<float>::quiet_NaN()}) {
            auto invalidDensity = Candidate(schema);
            invalidDensity.core.densities[0] = density;
            CheckError(CapturePointStorage(std::move(invalidDensity)), PCGErrors::PointDataInvalid);
        }
        auto invalidValue = Candidate(schema);
        std::get<PCGScalarColumn>(invalidValue.attributes[0].values)[0] = std::numeric_limits<double>::infinity();
        CheckError(CapturePointStorage(std::move(invalidValue)), PCGErrors::PointDataInvalid);

        auto boolSchema = Schema({{Key("pcg.boolean"), PCGAttributeType::Boolean}});
        auto invalidBoolean = Candidate(boolSchema);
        std::get<PCGBoolColumn>(invalidBoolean.attributes[0].values)[0] = 2;
        CheckError(CapturePointStorage(std::move(invalidBoolean)), PCGErrors::PointDataInvalid);

        auto vectorSchema = Schema({{Key("pcg.vector"), PCGAttributeType::Vector4}});
        auto invalidVector = Candidate(vectorSchema);
        std::get<PCGVector4Column>(invalidVector.attributes[0].values)[0].w = std::numeric_limits<float>::infinity();
        CheckError(CapturePointStorage(std::move(invalidVector)), PCGErrors::PointDataInvalid);
    }

    TEST_CASE("PCG point limit accepts exact boundary and rejects one over", "[unit][pcg][point]") {
        auto schema = Schema({});
        CHECK(CapturePointStorage(Candidate(schema, 16'384)).HasValue());
        CheckError(CapturePointStorage(Candidate(schema, 16'385)), PCGErrors::PointCapacityExceeded);
    }

    TEST_CASE("PCG rejected replacement preserves old immutable readers", "[unit][pcg][point]") {
        auto schema = Schema({{Key("pcg.value"), PCGAttributeType::Scalar}});
        auto old = CapturePointStorage(Candidate(schema)).Value();
        const auto *oldColumn = old->FindColumn<PCGScalarColumn>("pcg.value");
        REQUIRE(oldColumn != nullptr);
        auto invalid = Candidate(schema);
        invalid.core.densities[0] = 2.0F;
        CheckError(ReplacePointStorage(old, std::move(invalid)), PCGErrors::PointDataInvalid);
        CHECK(old->PointCount() == 2);
        CHECK(old->FindColumn<PCGScalarColumn>("pcg.value") == oldColumn);
        auto replacement = ReplacePointStorage(old, Candidate(schema, 1));
        REQUIRE(replacement.HasValue());
        CHECK(replacement.Value()->PointCount() == 1);
        CHECK(old->PointCount() == 2);
        CheckError(ReplacePointStorage({}, Candidate(schema)), PCGErrors::PointDataInvalid);
    }

    TEST_CASE("PCG checked byte arithmetic rejects add multiply and alignment overflow", "[unit][pcg][point]") {
        const auto maximum = std::numeric_limits<std::size_t>::max();
        CHECK(CheckedPCGAdd(maximum - 1, 1).Value() == maximum);
        CheckError(CheckedPCGAdd(maximum, 1), PCGErrors::PointSizeOverflow);
        CHECK(CheckedPCGMultiply(maximum, 1).Value() == maximum);
        CheckError(CheckedPCGMultiply(maximum, 2), PCGErrors::PointSizeOverflow);
        CHECK(CheckedPCGAlignUp(17, 8).Value() == 24);
        CheckError(CheckedPCGAlignUp(17, 0), PCGErrors::PointSizeOverflow);
        CheckError(CheckedPCGAlignUp(17, 3), PCGErrors::PointSizeOverflow);
        CheckError(CheckedPCGAlignUp(maximum, 8), PCGErrors::PointSizeOverflow);
    }

    TEST_CASE("PCG point errors are stable unique public descriptors", "[unit][pcg][point]") {
        const std::array descriptors{&PCGErrors::PointSchemaInvalid,    &PCGErrors::PointAttributeDuplicate,
                                     &PCGErrors::PointAttributeUnknown, &PCGErrors::PointAttributeTypeMismatch,
                                     &PCGErrors::PointDataInvalid,      &PCGErrors::PointCapacityExceeded,
                                     &PCGErrors::PointSizeOverflow};
        std::set<std::string_view> codes;
        for (const auto *descriptor : descriptors) {
            CHECK(descriptor->domain.Value() == "horo.pcg");
            CHECK(codes.insert(descriptor->code.Value()).second);
        }
    }
}  // namespace Horo::PCG
