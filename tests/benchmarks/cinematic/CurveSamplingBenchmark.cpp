#include "Horo/Cinematic/CurveSampling.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace {
    using namespace Horo::Cinematic;
    constexpr std::size_t KeyCount = 4'096;
    constexpr std::size_t SamplesPerBatch = 4'096;
    constexpr std::size_t MeasuredBatches = 1'000;
    constexpr double MaximumP99NanosecondsPerSample = 750.0;

    [[nodiscard]] std::vector<ScalarCurveKey> MakeKeys() {
        std::vector<ScalarCurveKey> keys(KeyCount);
        for (std::size_t index = 0; index < keys.size(); ++index) {
            keys[index] = {.time = static_cast<CurveTime>(index * 30),
                           .value = static_cast<float>((index * 37) % 101),
                           .interpolation = CurveInterpolation::CubicBezier,
                           .tangentIn = {-10, -0.5F},
                           .tangentOut = {10, 0.5F}};
        }
        return keys;
    }

    [[nodiscard]] double SampleBatch(const ScalarCurveView &curve, const std::size_t batch, double checksum) {
        const CurveTime last = curve.Keys().back().time;
        for (std::size_t sample = 0; sample < SamplesPerBatch; ++sample) {
            const std::uint64_t mixed = (sample * 2'654'435'761ULL) ^ (batch * 1'140'071'481ULL);
            const CurveTime time = static_cast<CurveTime>(mixed % static_cast<std::uint64_t>(last + 1));
            auto result = curve.Sample(time);
            if (result.HasError())
                throw std::runtime_error("Curve sampling unexpectedly failed");
            checksum += result.Value().value;
        }
        return checksum;
    }
}  // namespace

int main() {
#ifndef NDEBUG
    std::cerr << "Use a Release build without coverage or sanitizers for curve sampling qualification.\n";
    return 2;
#else
    const auto keys = MakeKeys();
    auto admitted = ScalarCurveView::Create(keys);
    if (admitted.HasError())
        throw std::runtime_error("Curve benchmark admission failed");
    const ScalarCurveView curve = admitted.Value();
    double checksum{};
    for (std::size_t warmup = 0; warmup < 100; ++warmup)
        checksum = SampleBatch(curve, warmup, checksum);

    std::vector<double> elapsed(MeasuredBatches);
    for (std::size_t batch = 0; batch < MeasuredBatches; ++batch) {
        const auto start = std::chrono::steady_clock::now();
        checksum = SampleBatch(curve, batch, checksum);
        const auto finish = std::chrono::steady_clock::now();
        elapsed[batch] = std::chrono::duration<double, std::nano>(finish - start).count() / SamplesPerBatch;
    }
    const double mean = std::accumulate(elapsed.begin(), elapsed.end(), 0.0) / MeasuredBatches;
    std::ranges::sort(elapsed);
    const double p99 = elapsed[MeasuredBatches * 99 / 100];
    std::cout << "keys,samples_per_batch,batches,mean_ns_per_sample,p99_ns_per_sample,max_ns_per_sample,budget_ns_per_sample,checksum\n"
              << KeyCount << ',' << SamplesPerBatch << ',' << MeasuredBatches << ',' << std::fixed << std::setprecision(3) << mean << ','
              << p99 << ',' << elapsed.back() << ',' << MaximumP99NanosecondsPerSample << ',' << checksum << '\n';
    if (p99 > MaximumP99NanosecondsPerSample)
        throw std::runtime_error("Curve sampling P99 exceeds the reviewed fixture budget");
#endif
}
