// The depth-block counter model behind real occlusion results (occlusion_model.h).
#include <cassert>
#include <cstdint>
#include <cstdio>

#include "gpu/shadps4/video_core/renderer_vulkan/occlusion_model.h"

using namespace OcclusionModel;

namespace {
// The words two dumps write into a block: the begin dump at +0, the end dump at +8.
void Dump(std::uint64_t* block, std::uint32_t pipes, std::uint64_t total, bool end) {
    for (std::uint32_t k = 0; k < pipes; ++k) {
        block[2 * k + (end ? 1 : 0)] = Share(total, pipes, k) | ValidBit;
    }
}

std::uint64_t SumOfDifferences(const std::uint64_t* block, std::uint32_t pipes) {
    std::uint64_t sum = 0;
    for (std::uint32_t k = 0; k < pipes; ++k) {
        sum += (block[2 * k + 1] & ~ValidBit) - (block[2 * k] & ~ValidBit);
    }
    return sum;
}
} // namespace

int main() {
    // The shares add up to the total, and every DB's share grows with it: the game's sum of
    // end - begin is the samples drawn in between, whatever the split.
    for (std::uint32_t pipes : {8u, 16u}) {
        for (std::uint64_t total = 0; total < 200; ++total) {
            std::uint64_t sum = 0;
            for (std::uint32_t k = 0; k < pipes; ++k) {
                sum += Share(total, pipes, k);
                assert(Share(total + 1, pipes, k) >= Share(total, pipes, k));
            }
            assert(sum == total);
        }
        for (std::uint64_t begin : {0ull, 7ull, 123456789ull}) {
            for (std::uint64_t samples : {0ull, 1ull, 5ull, 4000ull}) {
                std::uint64_t block[2 * MaxPipes] = {};
                assert(ReadBlock(block, pipes) == Block::NotReady); // zeroed by the game
                Dump(block, pipes, begin, false);
                assert(ReadBlock(block, pipes) == Block::NotReady); // only the begin landed
                Dump(block, pipes, begin + samples, true);
                assert(SumOfDifferences(block, pipes) == samples);
                assert(ReadBlock(block, pipes) == (samples ? Block::Visible : Block::Occluded));
            }
        }
    }

    // Anything not measured reads visible, never occluded.
    assert(Samples(0, false, true, 1u << 16) == Unmeasured);
    assert(Samples(0, false, false, 1u << 16) == Unmeasured);
    assert(Samples(1, true, false, 1u << 16) == Unmeasured); // imprecise 0/1: plainly visible
    assert(Samples(0, true, false, 1u << 16) == 0);
    assert(Samples(0, true, true, 4u << 16) == 0);           // occluded stays occluded
    assert(Samples(10, true, true, 1u << 16) == 10);
    assert(Samples(1, true, true, (1u << 16) + 1) == 2);      // rounds up, never down

    // Reduced scene targets count fewer samples: scaled back to 1920x1080, never below 1.
    assert(SampleScale(1920, 1080) == 1u << 16);
    assert(SampleScale(3840, 2160) == 1u << 16);
    assert(SampleScale(960, 540) == 4u << 16);
    assert(SampleScale(0, 0) == 1u << 16);
    assert(Samples(250, true, true, SampleScale(960, 540)) == 1000);

    // SET_PREDICATION: unknown draws; otherwise draws when visibility matches the request.
    assert(PredicatedDraws(false, true, true));
    assert(PredicatedDraws(false, true, false));
    assert(PredicatedDraws(true, false, true));
    assert(!PredicatedDraws(false, false, true));
    assert(PredicatedDraws(false, false, false));
    assert(!PredicatedDraws(true, false, false));

    // Hysteresis: the largest of the last three results; occluded only after three results in
    // a row found nothing, visible at once; an unseen past reads visible with the least count.
    {
        BlockHistory fresh;
        assert(fresh.Report(0) == BlockHistory::UnknownPast);
        assert(fresh.Report(0) == BlockHistory::UnknownPast);
        assert(fresh.Report(0) == 0);
        assert(fresh.Report(0) == 0);
        assert(fresh.Report(9) == 9); // visible at once
        assert(fresh.Report(0) == 9);
        assert(fresh.Report(0) == 9);
        assert(fresh.Report(0) == 0);
    }
    {
        // A lamp flickering 0/N/0 under jitter reads steadily visible.
        BlockHistory lamp;
        const std::uint64_t measured[] = {40, 0, 35, 0, 0, 41, 0, 38};
        for (const std::uint64_t m : measured) {
            assert(lamp.Report(m) >= 35);
        }
        // The largest of the last three: a drop shows after two more results.
        BlockHistory fading;
        fading.Report(100);
        fading.Report(100);
        fading.Report(100);
        assert(fading.Report(10) == 100);
        assert(fading.Report(10) == 100);
        assert(fading.Report(10) == 10);
        // Unmeasured (fail-safe visible) counts too.
        BlockHistory unknown;
        unknown.Report(0);
        unknown.Report(0);
        assert(unknown.Report(Unmeasured) == Unmeasured);
        assert(unknown.Report(0) == Unmeasured);
    }
    {
        // As the resolver writes it: the held count is added to the running total between the
        // begin and the end dump, so the game reads the held result from the block.
        for (std::uint32_t pipes : {8u, 16u}) {
            BlockHistory history;
            std::uint64_t total = 1000;
            const std::uint64_t measured[] = {12, 0, 0, 0, 5};
            const Block expected[] = {Block::Visible, Block::Visible, Block::Visible,
                                      Block::Occluded, Block::Visible};
            for (std::size_t i = 0; i < sizeof(measured) / sizeof(measured[0]); ++i) {
                std::uint64_t block[2 * MaxPipes] = {};
                Dump(block, pipes, total, false);
                const std::uint64_t begin = total;
                total += measured[i];
                const std::uint64_t reported = history.Report(total - begin);
                total += reported - (total - begin);
                Dump(block, pipes, total, true);
                assert(SumOfDifferences(block, pipes) == reported);
                assert(ReadBlock(block, pipes) == expected[i]);
            }
        }
    }

    std::printf("occlusion model: ok\n");
    return 0;
}
