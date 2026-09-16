#include "CbrReplayFile.h"
#include <cassert>
#include <iostream>

static AnnotatedReplay recording(bool facing, bool validCommand) {
    AnnotatedReplay replay("Test", "ny", "jb", 0, 1, 76561198000000001ULL);
    // Static initialization zeroes otherwise uninitialized legacy scalar fields.
    static const Metadata empty;
    for (int sample = 0; sample < 330; ++sample) {
        // Ten rolled-back frames before the final timeline resumes at frame 90.
        int frame = sample < 100 ? sample : sample - 10;
        auto metadata = std::make_shared<Metadata>(empty);
        metadata->SetFrameCount(frame);
        metadata->currentAction = {frame >= 270 && frame < 300 ? "TimelagShot" : "CmnActStand", "CmnActStand"};
        metadata->neutral = {frame < 270 || frame >= 300, true};
        metadata->facing = facing;
        metadata->hitMinX = 17; // Case construction must not change the source.
        int input = 5;
        if (validCommand) {
            if (frame == 266) input = 2;
            if (frame == 267) input = facing ? 3 : 1;
            if (frame == 268) input = facing ? 6 : 4;
            if (frame == 269) input = 128 + (facing ? 6 : 4);
        }
        if (frame == 270) input = 133;
        replay.AddFrame(metadata, input);
    }
    return replay;
}

static void preserved(AnnotatedReplay& replay, const std::vector<int>& inputs) {
    assert(replay.getInput() == inputs);
    assert(replay.MetadataSize() == 330);
    assert(replay.getSteamId() == 76561198000000001ULL);
    for (const auto& metadata : *replay.getAllMetadata()) assert(metadata->hitMinX == 17);
}

int main() {
    // Exact physical inputs at the three TimelagShot starts in the report.
    const std::vector<std::pair<bool, std::vector<int>>> recordedMotions = {
        {true, {2, 2, 2, 3, 3, 6, 6, 6, 6, 134, 133}},
        {false, {2, 2, 2, 1, 1, 1, 4, 132, 132}},
        {false, {2, 2, 2, 2, 1, 1, 1, 4, 4, 4, 5, 133, 133}}
    };
    for (const auto& motion : recordedMotions) {
        CbrReplayFile converter;
        std::string character = "ny", action = "TimelagShot";
        auto commands = converter.FetchCommandActions(character);
        auto pending = converter.MakeInputArray(action, commands, "CmnActStand");
        assert(!pending.empty());
        for (auto input = motion.second.rbegin(); input != motion.second.rend(); ++input)
            for (int part : converter.DeconstructInput(*input, motion.first))
                pending = converter.CheckCommandExecution(part, pending);
        assert(pending.empty());
    }
    for (bool facing : {false, true}) {
        auto replay = recording(facing, true);
        auto original = replay.getInput();
        CbrReplayFile converted;
        auto result = converted.makeFullCaseBase(&replay, "ny");
        if (result.errorCount) std::cerr << result.errorDetail.substr(0, 250) << '\n';
        assert(result.errorCount == 0);
        preserved(replay, original);
        auto count = converted.getCaseBaseLength();
        assert(count > 0);
        assert(converted.getCase(count - 1)->getEndIndex() == 319);
        assert(converted.getInput(269) == 128 + (facing ? 6 : 4));
        assert(converted.makeFullCaseBase(&replay, "ny").errorCount == 0);
        assert(converted.getCaseBaseLength() == count);
        preserved(replay, original);
    }

    auto bad = recording(false, false);
    auto original = bad.getInput();
    CbrReplayFile converted;
    auto first = converted.makeFullCaseBase(&bad, "ny");
    assert(first.errorCount == 1);
    assert(first.errorDetail.find("TimelagShot") != std::string::npos);
    assert(first.errorDetail.find("200 steps") != std::string::npos);
    assert(first.errorDetail.find("Frame: 319 ") != std::string::npos);
    assert(first.errorDetail.find("Frame: 320 ") == std::string::npos);
    assert(first.errorDetail.find("ERROR PTR") == std::string::npos);
    assert(first.errorDetail.find("testend") == std::string::npos);
    preserved(bad, original);
    auto count = converted.getCaseBaseLength();
    auto second = converted.makeFullCaseBase(&bad, "ny");
    assert(second.errorCount == first.errorCount && second.errorDetail == first.errorDetail);
    assert(converted.getCaseBaseLength() == count);
    preserved(bad, original);

    auto good = recording(false, true);
    assert(converted.makeFullCaseBase(&good, "ny").errorCount == 0);
    bad.getInputPtr()->pop_back();
    assert(converted.makeFullCaseBase(&bad, "ny").errorCount == 1);
    assert(converted.getCaseBaseLength() == 0);
    assert(converted.makeFullCaseBase(nullptr, "ny").errorCount == 1);
    std::cout << "CBR conversion: both facings, rollback, preservation, and retries passed\n";
}
