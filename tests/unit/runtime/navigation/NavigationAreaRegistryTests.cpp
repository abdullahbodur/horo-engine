#include "Horo/Navigation/NavigationAreas.h"
#include "Horo/Navigation/NavigationErrors.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <utility>

namespace Horo::Navigation {
    namespace {
        NavigationAreaId AreaId(const std::uint64_t value) {
            return NavigationAreaId::Create(value).Value();
        }

        NavigationFilterId FilterId(const std::uint64_t value) {
            return NavigationFilterId::Create(value).Value();
        }

        NavigationDescriptorSource Source(const NavigationDescriptorSourceKind kind, const std::uint64_t value) {
            return {.kind = kind, .id = NavigationDescriptorSourceId::Create(value).Value()};
        }

        NavigationAreaDescriptor Area(const std::uint64_t id, const NavigationDescriptorSource source, const float cost,
                                      const std::uint64_t flags) {
            return {.id = AreaId(id), .source = source, .traversalCost = cost, .flags = {.bits = flags}};
        }

        NavigationQueryFilterDescriptor Filter(const std::uint64_t id, const NavigationDescriptorSource source,
                                               const std::uint64_t included, const std::uint64_t excluded,
                                               std::vector<NavigationAreaCostOverride> overrides = {}) {
            return {.id = FilterId(id),
                    .source = source,
                    .includedFlags = {.bits = included},
                    .excludedFlags = {.bits = excluded},
                    .costOverrides = std::move(overrides)};
        }

        template <typename T> void RequireError(const Result<T> &result, const ErrorCodeDescriptor &expected) {
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().domain.Value() == expected.domain.Value());
            REQUIRE(result.ErrorValue().code.Value() == expected.code.Value());
        }

        NavigationAreaRegistry Registry() {
            const auto project = Source(NavigationDescriptorSourceKind::Project, 10);
            const auto package = Source(NavigationDescriptorSourceKind::Package, 20);
            const std::array areas{Area(9, package, 4.0F, 0b010), Area(3, project, 1.5F, 0b001)};
            const std::array filters{
                Filter(8, package, 0, 0),
                Filter(2, project, 0b011, 0b010, {{.area = AreaId(3), .traversalCost = 0.25F}}),
            };
            return std::move(NavigationAreaRegistry::Create(areas, filters)).Value();
        }
    }  // namespace

    TEST_CASE("Navigation descriptors resolve deterministically across project package and input order", "[unit][navigation][area]") {
        const auto first = Registry();
        const auto project = Source(NavigationDescriptorSourceKind::Project, 10);
        const auto package = Source(NavigationDescriptorSourceKind::Package, 20);
        const std::array reversedAreas{Area(3, project, 1.5F, 0b001), Area(9, package, 4.0F, 0b010)};
        const std::array reversedFilters{
            Filter(2, project, 0b011, 0b010, {{.area = AreaId(3), .traversalCost = 0.25F}}),
            Filter(8, package, 0, 0),
        };
        const auto second = std::move(NavigationAreaRegistry::Create(reversedAreas, reversedFilters)).Value();

        REQUIRE(first.Areas()[0].id == AreaId(3));
        REQUIRE(first.Areas()[1].id == AreaId(9));
        REQUIRE(first.Filters()[0].id == FilterId(2));
        REQUIRE(first.Filters()[1].id == FilterId(8));
        REQUIRE(second.Areas()[0].id == first.Areas()[0].id);
        REQUIRE(second.Areas()[1].id == first.Areas()[1].id);
        REQUIRE(second.Filters()[0].id == first.Filters()[0].id);
        REQUIRE(second.Filters()[1].id == first.Filters()[1].id);
    }

    TEST_CASE("Navigation traversal uses exclusion wins wildcard inclusion and exact cost overrides", "[unit][navigation][area]") {
        const auto registry = Registry();

        const auto included = registry.ResolveTraversal(FilterId(2), AreaId(3));
        REQUIRE(included.HasValue());
        REQUIRE(included.Value().traversable);
        REQUIRE(included.Value().traversalCost == 0.25F);

        const auto excluded = registry.ResolveTraversal(FilterId(2), AreaId(9));
        REQUIRE(excluded.HasValue());
        REQUIRE_FALSE(excluded.Value().traversable);
        REQUIRE(excluded.Value().traversalCost == 4.0F);

        const auto wildcard = registry.ResolveTraversal(FilterId(8), AreaId(9));
        REQUIRE(wildcard.HasValue());
        REQUIRE(wildcard.Value().traversable);
        REQUIRE(wildcard.Value().traversalCost == 4.0F);

        const auto source = Source(NavigationDescriptorSourceKind::Project, 30);
        const std::array includeMissAreas{Area(30, source, 2.0F, 0b100)};
        const std::array includeMissFilters{Filter(30, source, 0b001, 0)};
        const auto includeMissRegistry = std::move(NavigationAreaRegistry::Create(includeMissAreas, includeMissFilters)).Value();
        const auto includeMiss = includeMissRegistry.ResolveTraversal(FilterId(30), AreaId(30));
        REQUIRE(includeMiss.HasValue());
        REQUIRE_FALSE(includeMiss.Value().traversable);
    }

    TEST_CASE("Navigation registry rejects malformed identities sources and all invalid cost classes", "[unit][navigation][area]") {
        const auto source = Source(NavigationDescriptorSourceKind::Project, 1);
        const std::array invalidCosts{-1.0F, std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                                      std::numeric_limits<float>::quiet_NaN()};
        for (const float cost : invalidCosts) {
            const std::array areas{Area(1, source, cost, 1)};
            RequireError(NavigationAreaRegistry::Create(areas, {}), NavigationErrors::AreaDescriptorInvalid);
        }

        const std::array areas{Area(1, source, 0.0F, 1)};
        REQUIRE(NavigationAreaRegistry::Create(areas, {}).HasValue());
        auto invalidArea = areas[0];
        invalidArea.id = {};
        RequireError(NavigationAreaRegistry::Create(std::span{&invalidArea, 1}, {}), NavigationErrors::AreaDescriptorInvalid);
        invalidArea = areas[0];
        invalidArea.source.kind = NavigationDescriptorSourceKind::Count;
        RequireError(NavigationAreaRegistry::Create(std::span{&invalidArea, 1}, {}), NavigationErrors::AreaDescriptorInvalid);
        invalidArea = areas[0];
        invalidArea.source.id = {};
        RequireError(NavigationAreaRegistry::Create(std::span{&invalidArea, 1}, {}), NavigationErrors::AreaDescriptorInvalid);

        for (const float cost : invalidCosts) {
            const std::array filters{Filter(1, source, 0, 0, {{.area = AreaId(1), .traversalCost = cost}})};
            RequireError(NavigationAreaRegistry::Create(areas, filters), NavigationErrors::FilterDescriptorInvalid);
        }
        auto invalidFilter = Filter(1, source, 0, 0);
        invalidFilter.id = {};
        RequireError(NavigationAreaRegistry::Create(areas, std::span{&invalidFilter, 1}), NavigationErrors::FilterDescriptorInvalid);
        invalidFilter = Filter(1, source, 0, 0);
        invalidFilter.source.id = {};
        RequireError(NavigationAreaRegistry::Create(areas, std::span{&invalidFilter, 1}), NavigationErrors::FilterDescriptorInvalid);
    }

    TEST_CASE("Navigation registry rejects every identity collision independent of descriptor origin", "[unit][navigation][area]") {
        const auto project = Source(NavigationDescriptorSourceKind::Project, 1);
        const auto package = Source(NavigationDescriptorSourceKind::Package, 2);
        const std::array duplicateAreas{Area(1, project, 1.0F, 1), Area(1, package, 2.0F, 2)};
        RequireError(NavigationAreaRegistry::Create(duplicateAreas, {}), NavigationErrors::DescriptorConflict);

        const std::array areas{Area(1, project, 1.0F, 1)};
        const std::array duplicateFilters{Filter(3, project, 0, 0), Filter(3, package, 0, 0)};
        RequireError(NavigationAreaRegistry::Create(areas, duplicateFilters), NavigationErrors::DescriptorConflict);

        const std::array duplicateOverrides{
            Filter(3, project, 0, 0, {{.area = AreaId(1), .traversalCost = 1.0F}, {.area = AreaId(1), .traversalCost = 2.0F}})};
        RequireError(NavigationAreaRegistry::Create(areas, duplicateOverrides), NavigationErrors::DescriptorConflict);
    }

    TEST_CASE("Navigation registry never substitutes defaults for unknown area or filter identities", "[unit][navigation][area]") {
        const auto registry = Registry();
        RequireError(registry.ResolveArea(AreaId(404)), NavigationErrors::AreaUnknown);
        RequireError(registry.ResolveFilter(FilterId(404)), NavigationErrors::FilterUnknown);
        RequireError(registry.ResolveTraversal(FilterId(2), AreaId(404)), NavigationErrors::AreaUnknown);
        RequireError(registry.ResolveTraversal(FilterId(404), AreaId(3)), NavigationErrors::FilterUnknown);

        const auto source = Source(NavigationDescriptorSourceKind::Project, 1);
        const std::array areas{Area(1, source, 1.0F, 1)};
        const std::array filterWithMissingArea{Filter(1, source, 0, 0, {{.area = AreaId(404), .traversalCost = 2.0F}})};
        RequireError(NavigationAreaRegistry::Create(areas, filterWithMissingArea), NavigationErrors::AreaUnknown);
    }
}  // namespace Horo::Navigation
