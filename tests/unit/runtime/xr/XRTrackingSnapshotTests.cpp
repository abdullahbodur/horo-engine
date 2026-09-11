#include "Horo/XR/XRErrors.h"
#include "Horo/XR/XRTrackingSnapshot.h"
#include "support/AllocationProbe.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <type_traits>
#include <vector>

namespace Horo::XR {
    namespace {
        template <typename Generation> [[nodiscard]] Generation GenerationValue(const std::uint64_t value) {
            auto result = Generation::Create(value);
            REQUIRE(result.HasValue());
            return result.Value();
        }

        [[nodiscard]] XRSessionId ActiveSession(const std::uint32_t generation = 5) {
            return {{GenerationValue<XRRuntimeGeneration>(1), {2, 3}}, {4, generation}};
        }

        [[nodiscard]] XRCoordinateSpace TrackingSpace(const XRSessionId &session, const XRSpaceKind kind, const std::uint32_t index) {
            return {.id = {session, {index, 1}}, .kind = kind, .worldOriginRevision = GenerationValue<XRWorldOriginRevision>(9)};
        }

        [[nodiscard]] XRPoseSample PoseSample(const XRSessionId &session, const XRRuntimeTime sampledAt) {
            const auto trackedPosition =
                XRPoseComponent<Math::Vec3>{.value = Math::Vec3{1.0F, 2.0F, 3.0F}, .validity = XRPoseComponentValidity::Tracked};
            const auto trackedOrientation =
                XRPoseComponent<Math::Quaternion>{.value = Math::Quaternion::Identity(), .validity = XRPoseComponentValidity::Tracked};
            auto result = XRPoseSample::Create({.session = session,
                                                .source = TrackingSpace(session, XRSpaceKind::View, 11),
                                                .target = TrackingSpace(session, XRSpaceKind::Local, 12),
                                                .purpose = XRPosePurpose::SimulationInput,
                                                .time = {.runtimeSample = sampledAt, .simulation = GenerationValue<XRSimulationTime>(80)},
                                                .components = {.positionMeters = trackedPosition,
                                                               .orientation = trackedOrientation,
                                                               .confidence = XRTrackingConfidence::High,
                                                               .loss = XRTrackingLossState::None}},
                                               session, GenerationValue<XRWorldOriginRevision>(9));
            REQUIRE(result.HasValue());
            return std::move(result).Value();
        }

        [[nodiscard]] XRTrackedDeviceCapabilities Capabilities(const std::initializer_list<XRTrackedDeviceCapability> capabilities) {
            XRTrackedDeviceCapabilities result;
            for (const auto capability : capabilities)
                result.values[static_cast<std::size_t>(capability)] = true;
            return result;
        }

        class TrackingFixture final {
        public:
            TrackingFixture()
                : session(ActiveSession()), sampledAt(GenerationValue<XRRuntimeTime>(100)),
                  origin(GenerationValue<XRWorldOriginRevision>(9)), revision(GenerationValue<XRTrackingSnapshotRevision>(7)),
                  head{.id = {session, {20, 1}},
                       .role = XRTrackedDeviceRole::Head,
                       .state = XRTrackedDeviceState::Tracked,
                       .capabilities = Capabilities({XRTrackedDeviceCapability::HeadPose})},
                  controller{.id = {session, {21, 1}},
                             .role = XRTrackedDeviceRole::LeftController,
                             .state = XRTrackedDeviceState::Tracked,
                             .capabilities = Capabilities({XRTrackedDeviceCapability::GripPose, XRTrackedDeviceCapability::AimPose})} {
                devices = {head, controller};
                poses.emplace_back(XRTrackedPoseRecord{head.id, XRTrackedPoseKind::Head, PoseSample(session, sampledAt)});
                poses.emplace_back(XRTrackedPoseRecord{controller.id, XRTrackedPoseKind::Grip, PoseSample(session, sampledAt)});
            }

            [[nodiscard]] XRTrackingSnapshotDescriptor Descriptor() const {
                return {.session = session,
                        .revision = revision,
                        .sequence = 41,
                        .sampledAt = sampledAt,
                        .worldOriginRevision = origin,
                        .focus = XRTrackingFocusState::Focused,
                        .visibility = XRTrackingVisibilityState::Visible,
                        .state = XRTrackingState::Active,
                        .limits = {.maximumDevices = 8, .maximumPoses = 16},
                        .devices = devices,
                        .poses = poses};
            }

            [[nodiscard]] XRTrackingSnapshot Snapshot() const {
                auto result = XRTrackingSnapshot::Create(Descriptor(), session, origin);
                REQUIRE(result.HasValue());
                return std::move(result).Value();
            }

            XRSessionId session;
            XRRuntimeTime sampledAt;
            XRWorldOriginRevision origin;
            XRTrackingSnapshotRevision revision;
            XRTrackedDeviceRecord head;
            XRTrackedDeviceRecord controller;
            std::vector<XRTrackedDeviceRecord> devices;
            std::vector<XRTrackedPoseRecord> poses;
        };

        template <typename Value> [[nodiscard]] bool HasErrorIdentity(const Result<Value> &result, const ErrorCodeDescriptor &descriptor) {
            return result.HasError() && result.ErrorValue().domain.Value() == descriptor.domain.Value() &&
                   result.ErrorValue().code.Value() == descriptor.code.Value();
        }
    }  // namespace

    TEST_CASE("XR tracking snapshots own bounded canonical evidence", "[unit][xr][tracking-snapshot]") {
        TrackingFixture fixture;
        auto snapshot = XRTrackingSnapshot::Create(fixture.Descriptor(), fixture.session, fixture.origin);
        REQUIRE(snapshot.HasValue());
        REQUIRE(snapshot.Value().Revision() == fixture.revision);
        REQUIRE(snapshot.Value().Sequence() == 41);
        REQUIRE(snapshot.Value().SampledAt() == fixture.sampledAt);
        REQUIRE(snapshot.Value().Focus() == XRTrackingFocusState::Focused);
        REQUIRE(snapshot.Value().Visibility() == XRTrackingVisibilityState::Visible);
        REQUIRE(snapshot.Value().Devices().size() == 2);
        REQUIRE(snapshot.Value().Poses().size() == 2);

        fixture.devices.clear();
        fixture.poses.clear();
        REQUIRE(snapshot.Value().Devices().size() == 2);
        static_assert(std::is_same_v<decltype(snapshot.Value().Devices()), std::span<const XRTrackedDeviceRecord>>);
    }

    TEST_CASE("XR tracking queries are allocation-free and preserve exact confidence", "[unit][xr][tracking-snapshot]") {
        const TrackingFixture fixture;
        const auto snapshot = fixture.Snapshot();
        const auto before = Tests::AllocationProbe::Count();
        auto pose =
            QueryXRTrackedPose(snapshot, fixture.controller.id, XRTrackedPoseKind::Grip, fixture.session, fixture.revision, fixture.origin);
        const auto after = Tests::AllocationProbe::Count();
        REQUIRE(pose.HasValue());
        REQUIRE(pose.Value().Components().confidence == XRTrackingConfidence::High);
        REQUIRE(pose.Value().Components().positionMeters.validity == XRPoseComponentValidity::Tracked);
        CHECK(after == before);
    }

    TEST_CASE("XR tracking snapshots reject malformed versions bounds and canonical order", "[unit][xr][tracking-snapshot]") {
        TrackingFixture fixture;
        auto descriptor = fixture.Descriptor();
        descriptor.contractVersion = {};
        REQUIRE(
            HasErrorIdentity(XRTrackingSnapshot::Create(descriptor, fixture.session, fixture.origin), XRErrors::ContractVersionInvalid));

        descriptor = fixture.Descriptor();
        descriptor.limits.maximumDevices = 1;
        REQUIRE(HasErrorIdentity(XRTrackingSnapshot::Create(descriptor, fixture.session, fixture.origin), XRErrors::CapacityExceeded));

        descriptor = fixture.Descriptor();
        descriptor.limits = {.maximumDevices = XRTrackingSnapshotHardLimits::MaximumDevices,
                             .maximumPoses = XRTrackingSnapshotHardLimits::MaximumPoses};
        REQUIRE(XRTrackingSnapshot::Create(descriptor, fixture.session, fixture.origin).HasValue());

        descriptor.limits.maximumPoses = XRTrackingSnapshotHardLimits::MaximumPoses + 1;
        REQUIRE(HasErrorIdentity(XRTrackingSnapshot::Create(descriptor, fixture.session, fixture.origin), XRErrors::CapacityExceeded));

        descriptor = fixture.Descriptor();
        std::swap(fixture.devices[0], fixture.devices[1]);
        descriptor.devices = fixture.devices;
        REQUIRE(
            HasErrorIdentity(XRTrackingSnapshot::Create(descriptor, fixture.session, fixture.origin), XRErrors::TrackingSnapshotInvalid));

        fixture.devices = {fixture.head, fixture.controller};
        fixture.devices.push_back(fixture.controller);
        descriptor = fixture.Descriptor();
        REQUIRE(
            HasErrorIdentity(XRTrackingSnapshot::Create(descriptor, fixture.session, fixture.origin), XRErrors::TrackingSnapshotInvalid));

        fixture.devices = {fixture.head, fixture.controller};
        auto duplicateHead = fixture.head;
        duplicateHead.id.slot.index = 22;
        fixture.devices.push_back(duplicateHead);
        descriptor = fixture.Descriptor();
        REQUIRE(
            HasErrorIdentity(XRTrackingSnapshot::Create(descriptor, fixture.session, fixture.origin), XRErrors::TrackingSnapshotInvalid));

        fixture.devices = {fixture.head, fixture.controller};
        fixture.devices[1].capabilities.values[static_cast<std::size_t>(XRTrackedDeviceCapability::HeadPose)] = true;
        descriptor = fixture.Descriptor();
        REQUIRE(
            HasErrorIdentity(XRTrackingSnapshot::Create(descriptor, fixture.session, fixture.origin), XRErrors::TrackingSnapshotInvalid));

        fixture.devices = {fixture.head, fixture.controller};
        fixture.devices[1].role = XRTrackedDeviceRole::Count;
        descriptor = fixture.Descriptor();
        REQUIRE(
            HasErrorIdentity(XRTrackingSnapshot::Create(descriptor, fixture.session, fixture.origin), XRErrors::TrackingSnapshotInvalid));
    }

    TEST_CASE("XR tracking snapshots reject contradictory device and pose evidence", "[unit][xr][tracking-snapshot]") {
        TrackingFixture fixture;
        auto descriptor = fixture.Descriptor();
        fixture.devices[0].state = XRTrackedDeviceState::TrackingLost;
        descriptor.devices = fixture.devices;
        REQUIRE(
            HasErrorIdentity(XRTrackingSnapshot::Create(descriptor, fixture.session, fixture.origin), XRErrors::TrackingSnapshotInvalid));

        fixture.devices[0] = fixture.head;
        fixture.poses[0].kind = XRTrackedPoseKind::Grip;
        descriptor = fixture.Descriptor();
        REQUIRE(
            HasErrorIdentity(XRTrackingSnapshot::Create(descriptor, fixture.session, fixture.origin), XRErrors::TrackingSnapshotInvalid));

        fixture.poses[0] =
            XRTrackedPoseRecord{fixture.head.id, XRTrackedPoseKind::Head, PoseSample(fixture.session, GenerationValue<XRRuntimeTime>(101))};
        descriptor = fixture.Descriptor();
        REQUIRE(
            HasErrorIdentity(XRTrackingSnapshot::Create(descriptor, fixture.session, fixture.origin), XRErrors::TrackingSnapshotInvalid));
    }

    TEST_CASE("XR tracking loss clears all pose evidence without fabricating a replacement", "[unit][xr][tracking-snapshot]") {
        TrackingFixture fixture;
        fixture.poses.clear();
        for (auto &device : fixture.devices)
            device.state = XRTrackedDeviceState::TrackingLost;
        auto descriptor = fixture.Descriptor();
        descriptor.state = XRTrackingState::TrackingLost;
        auto snapshot = XRTrackingSnapshot::Create(descriptor, fixture.session, fixture.origin);
        REQUIRE(snapshot.HasValue());
        REQUIRE(snapshot.Value().Poses().empty());
        REQUIRE(HasErrorIdentity(QueryXRTrackedPose(snapshot.Value(), fixture.head.id, XRTrackedPoseKind::Head, fixture.session,
                                                    fixture.revision, fixture.origin),
                                 XRErrors::OperationUnavailable));

        descriptor.state = XRTrackingState::PermissionLost;
        REQUIRE(
            HasErrorIdentity(XRTrackingSnapshot::Create(descriptor, fixture.session, fixture.origin), XRErrors::TrackingSnapshotInvalid));
        fixture.devices.clear();
        descriptor = fixture.Descriptor();
        descriptor.state = XRTrackingState::PermissionLost;
        REQUIRE(XRTrackingSnapshot::Create(descriptor, fixture.session, fixture.origin).HasValue());
    }

    TEST_CASE("XR tracking fences replacement shutdown revision and origin", "[unit][xr][tracking-snapshot]") {
        const TrackingFixture fixture;
        const auto snapshot = fixture.Snapshot();
        REQUIRE(HasErrorIdentity(ValidateXRTrackingSnapshot(snapshot, ActiveSession(6), fixture.revision, fixture.origin),
                                 XRErrors::IdentityStale));
        REQUIRE(HasErrorIdentity(ValidateXRTrackingSnapshot(snapshot, {}, fixture.revision, fixture.origin), XRErrors::IdentityInvalid));
        REQUIRE(HasErrorIdentity(ValidateXRTrackingSnapshot(snapshot, fixture.session, GenerationValue<XRTrackingSnapshotRevision>(8),
                                                            fixture.origin),
                                 XRErrors::TrackingSnapshotStale));
        REQUIRE(HasErrorIdentity(ValidateXRTrackingSnapshot(snapshot, fixture.session, fixture.revision,
                                                            GenerationValue<XRWorldOriginRevision>(10)),
                                 XRErrors::OriginRevisionStale));

        auto replacedDevice = fixture.controller.id;
        ++replacedDevice.slot.generation;
        REQUIRE(HasErrorIdentity(QueryXRTrackedPose(snapshot, replacedDevice, XRTrackedPoseKind::Grip, fixture.session, fixture.revision,
                                                    fixture.origin),
                                 XRErrors::IdentityStale));
    }

    TEST_CASE("XR tracking queries distinguish unsupported from unavailable", "[unit][xr][tracking-snapshot]") {
        const TrackingFixture fixture;
        const auto snapshot = fixture.Snapshot();
        REQUIRE(HasErrorIdentity(QueryXRTrackedPose(snapshot, fixture.controller.id, XRTrackedPoseKind::Palm, fixture.session,
                                                    fixture.revision, fixture.origin),
                                 XRErrors::OperationUnsupported));

        auto unknown = fixture.controller.id;
        unknown.slot.index = 99;
        REQUIRE(HasErrorIdentity(QueryXRTrackedPose(snapshot, unknown, XRTrackedPoseKind::Grip, fixture.session, fixture.revision,
                                                    fixture.origin),
                                 XRErrors::OperationUnavailable));
        REQUIRE(HasErrorIdentity(QueryXRTrackedPose(snapshot, fixture.controller.id, XRTrackedPoseKind::Aim, fixture.session,
                                                    fixture.revision, fixture.origin),
                                 XRErrors::OperationUnavailable));
        REQUIRE(HasErrorIdentity(QueryXRTrackedPose(snapshot, fixture.controller.id, XRTrackedPoseKind::Count, fixture.session,
                                                    fixture.revision, fixture.origin),
                                 XRErrors::OperationInvalid));
    }
}  // namespace Horo::XR
