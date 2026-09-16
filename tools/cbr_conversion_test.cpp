#include "CbrReplayFile.h"
#include <cassert>
#include <iostream>
#include <sstream>

static AnnotatedReplay recording(bool facing, bool validCommand, bool rollback = true,
    std::string priorAction = "CmnActStand", bool rollbackAtCommand = false) {
    AnnotatedReplay replay("Test", "ny", "jb", 0, 1, 76561198000000001ULL);
    // Static initialization zeroes otherwise uninitialized legacy scalar fields.
    static const Metadata empty;
    const int rewindAt = rollbackAtCommand ? 274 : 100;
    for (int sample = 0; sample < (rollback ? 330 : 320); ++sample) {
        int frame = rollback && sample >= rewindAt ? sample - 10 : sample;
        auto metadata = std::make_shared<Metadata>(empty);
        metadata->SetFrameCount(frame);
        metadata->currentAction = {frame >= 270 && frame < 300 ? "TimelagShot" : "CmnActStand", "CmnActStand"};
        metadata->neutral = {frame < 270 || frame >= 300, true};
        if (priorAction != "CmnActStand" && frame >= 250 && frame < 270) {
            metadata->currentAction[0] = priorAction;
            metadata->neutral[0] = false;
            metadata->hitMinX = -1;
        }
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
        if (priorAction != "CmnActStand") {
            const bool sickle = priorAction == "GedanShot";
            if (frame == 246) input = 2;
            if (frame == 247) input = (sickle != facing) ? 3 : 1;
            if (frame == 248) input = (sickle != facing) ? 6 : 4;
            if (frame == 249) input = (sickle ? 128 : 32) + ((sickle != facing) ? 6 : 4);
            if (frame == 250) input = (sickle ? 128 : 32) + 5;
            metadata->hitMinX = -1;
            metadata->hitThisFrame[1] = sickle && frame == 266;
        }
        if (rollback && rollbackAtCommand && sample >= 264 && sample < 274) {
            // Bad speculative data must disappear before command matching.
            input = 9;
            metadata->currentAction[0] = "PredictionDiscarded";
            metadata->facing = !facing;
        }
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

static bool resolvesOrb(const std::vector<int>& inputs, bool facing, const std::string& action) {
    CbrReplayFile converter;
    std::string character = "kg", move = action;
    auto commands = converter.FetchCommandActions(character);
    auto pending = converter.MakeInputArray(move, commands, "CmnActStand");
    assert(!pending.empty());
    for (auto it = inputs.rbegin(); it != inputs.rend(); ++it)
        for (int part : converter.DeconstructInput(*it, facing))
            pending = converter.CheckCommandExecution(part, pending);
    return pending.empty();
}

static AnnotatedReplay orbRecording(bool facing, bool rollback, int release, int button) {
    AnnotatedReplay replay("Test", "kg", "jb", 0, 1);
    static const Metadata empty;
    for (int sample = 0; sample < (rollback ? 330 : 320); ++sample) {
        const int frame = rollback && sample >= 274 ? sample - 10 : sample;
        auto metadata = std::make_shared<Metadata>(empty);
        metadata->SetFrameCount(frame);
        metadata->facing = facing;
        const bool orb = frame >= 270 && frame < 300;
        metadata->currentAction = {orb ? (button == 16 ? "ShotA" : "ShotB") : "CmnActStand", "CmnActStand"};
        metadata->neutral = {!orb, true};
        int input = 5;
        if (frame >= 220 && frame <= 264) input = 1; // 45 frames of down-back charge.
        if (frame >= 265 && frame <= 270) input = release + (frame >= 269 ? button : 0);
        if (facing) input = replay.inverseInput(input);
        if (rollback && sample >= 264 && sample < 274) {
            input = 5;
            metadata->currentAction[0] = "PredictionDiscarded";
        }
        replay.AddFrame(metadata, input);
    }
    return replay;
}

static void testKaguraOrbs() {
    // Physical input/count runs from the two reported ShotA failures, each
    // including the 200 preceding samples. No player identity is retained.
    const std::vector<std::vector<std::pair<int, int>>> captured = {
        {{5,20},{6,3},{5,2},{6,2},{38,6},{6,2},{70,7},{6,34},{134,11},{6,8},
         {1,10},{17,3},{1,9},{2,1},{3,22},{131,12},{147,1},{19,5},{3,35},{1,3},{17,5}},
        {{37,1},{5,23},{37,5},{5,1},{2,9},{34,5},{2,2},{1,2},{4,6},{36,7},
         {4,2},{1,1},{2,8},{66,6},{2,25},{130,10},{146,3},{24,5},{8,30},
         {9,8},{25,6},{9,28},{6,4},{2,1},{18,1},{17,2}}
    };
    AnnotatedReplay mirror;
    int unresolved = 0;
    for (const auto& runs : captured) {
        std::vector<int> inputs;
        for (const auto& run : runs) inputs.insert(inputs.end(), run.second, run.first);
        assert(inputs.size() == 201);
        unresolved += !resolvesOrb(inputs, true, "ShotA");
        for (int& input : inputs) input = mirror.inverseInput(input);
        unresolved += !resolvesOrb(inputs, false, "ShotA");
    }
    std::cerr << "Kagura captured/mirrored unresolved motions: " << unresolved << "/4\n";
    assert(unresolved == 0);

    for (bool facing : {false, true}) {
        for (int button : {16, 32}) {
            const std::string action = button == 16 ? "ShotA" : "ShotB";
            for (int chargeDirection : {1, 4, 7}) {
                for (int release : {3, 6, 9}) {
                    std::vector<int> inputs(45, chargeDirection);
                    inputs.push_back(release + button);
                    if (facing) for (int& input : inputs) input = mirror.inverseInput(input);
                    assert(resolvesOrb(inputs, facing, action));
                    inputs.erase(inputs.begin()); // The fix must still require the charge.
                    assert(!resolvesOrb(inputs, facing, action));
                }
            }
            for (int release : {1, 2, 4, 5, 7, 8}) {
                std::vector<int> inputs(45, 4);
                inputs.push_back(release + button);
                if (facing) for (int& input : inputs) input = mirror.inverseInput(input);
                assert(!resolvesOrb(inputs, facing, action));
            }
            std::vector<int> missingButton(45, 4);
            missingButton.push_back(6);
            if (facing) for (int& input : missingButton) input = mirror.inverseInput(input);
            assert(!resolvesOrb(missingButton, facing, action));
            for (int release : {3, 6, 9}) {
                std::string reference;
                for (bool rollback : {false, true}) {
                    auto replay = orbRecording(facing, rollback, release, button);
                    const auto original = replay.getInput();
                    CbrReplayFile converted(replay.getCharacterName(), replay.getCharIds());
                    const auto result = converted.makeFullCaseBase(&replay, "kg");
                    assert(result.errorCount == 0);
                    assert(replay.getInput() == original);
                    assert(result.diagnosticSummary.find(rollback ? "removed=10" : "removed=0") != std::string::npos);
                    std::ostringstream out;
                    { boost::archive::text_oarchive writer(out); writer << converted; }
                    if (!rollback) reference = out.str();
                    else assert(out.str() == reference);
                }
            }
        }
    }
}

int main() {
    testKaguraOrbs();
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
    assert(first.errorDetail.find("counter=200") != std::string::npos);
    assert(first.errorDetail.find("cleaned sample=319 source_sample=329") != std::string::npos);
    assert(first.errorDetail.find("cleaned sample=320 ") == std::string::npos);
    assert(first.errorDetail.find("raw sample=329 ") != std::string::npos);
    assert(first.errorDetail.find("[COMMAND_UNRESOLVED]") != std::string::npos);
    assert(first.errorDetail.find("[TIMELINE_INVALID]") == std::string::npos);
    assert(first.errorDetail.find("expected=TimelagShot[2 1 4 D]") != std::string::npos);
    assert(first.diagnosticSummary.find("removed=10") != std::string::npos);
    assert(first.diagnosticSummary.find("cleaned_non_increasing=0") != std::string::npos);
    assert(first.errorDetail.find("ERROR PTR") == std::string::npos);
    assert(first.errorDetail.find("testend") == std::string::npos);
    preserved(bad, original);
    auto count = converted.getCaseBaseLength();
    auto second = converted.makeFullCaseBase(&bad, "ny");
    assert(second.errorCount == first.errorCount && second.errorDetail == first.errorDetail);
    assert(converted.getCaseBaseLength() == count);
    preserved(bad, original);

    // The final recording is identical with and without speculative frames,
    // including rewinds across both the buffered input and move transition.
    const auto archive = [](CbrReplayFile& replay) {
        std::ostringstream out;
        { boost::archive::text_oarchive writer(out); writer << replay; }
        return out.str();
    };
    for (bool facing : {false, true}) {
        for (const std::string prior : {"CmnActStand", "SlowFieldB", "GedanShot"}) {
            std::string reference;
            for (bool rollback : {false, true}) {
                auto replay = recording(facing, true, rollback, prior, true);
                CbrReplayFile result(replay.getCharacterName(), replay.getCharIds());
                auto error = result.makeFullCaseBase(&replay, "ny");
                if (error.errorCount) std::cerr << error.errorDetail.substr(0, 700) << '\n';
                assert(error.errorCount == 0);
                assert(error.diagnosticSummary.find(rollback ? "removed=10" : "removed=0") != std::string::npos);
                if (!rollback) reference = archive(result);
                else assert(archive(result) == reference);
            }
        }
        // Another character's command uses the same timeline-cleanup machinery.
        std::string reference;
        for (bool rollback : {false, true}) {
            auto replay = recording(facing, true, rollback, "CmnActStand", true);
            for (int i = 0; i < replay.MetadataSize(); ++i) {
                auto metadata = replay.CopyMetadataPtr(i);
                if (metadata->currentAction[0] == "PredictionDiscarded") continue;
                if (metadata->currentAction[0] == "TimelagShot") metadata->currentAction[0] = "Shot";
                int frame = metadata->getFrameCount();
                if (frame >= 266 && frame <= 269)
                    (*replay.getInputPtr())[i] = replay.inverseInput((*replay.getInputPtr())[i]);
            }
            CbrReplayFile result;
            auto error = result.makeFullCaseBase(&replay, "rg");
            assert(error.errorCount == 0);
            // Avoid uninitialized default character IDs in the archive.
            result.getCharIds() = {0, 1};
            if (!rollback) reference = archive(result);
            else assert(archive(result) == reference);
        }
    }

    for (bool rollback : {false, true}) {
        auto missingMotion = recording(true, false, rollback, "GedanShot", true);
        CbrReplayFile result;
        auto error = result.makeFullCaseBase(&missingMotion, "ny");
        assert(error.errorCount > 0);
        assert(error.errorDetail.find("[COMMAND_UNRESOLVED]") != std::string::npos);
        assert(error.errorDetail.find("previous_action=GedanShot") != std::string::npos);
        assert(error.errorDetail.find("[TIMELINE_INVALID]") == std::string::npos);
        assert(error.diagnosticSummary.find("cleaned_non_increasing=0") != std::string::npos);
        if (rollback) {
            assert(error.errorDetail.find("raw sample=264 source_sample=264 game_frame=264 retained=0") != std::string::npos);
            assert(error.errorDetail.find("cleaned sample=270 source_sample=280") != std::string::npos);
        }
    }
    auto baseReplay = recording(false, true, false);
    CbrReplayFile baseResult(baseReplay.getCharacterName(), baseReplay.getCharIds());
    assert(baseResult.makeFullCaseBase(&baseReplay, "ny").errorCount == 0);
    auto duplicateFrame = baseReplay;
    auto predicted = std::make_shared<Metadata>(*baseReplay.CopyMetadataPtr(267));
    predicted->currentAction[0] = "PredictionDiscarded";
    duplicateFrame.getAllMetadata()->insert(duplicateFrame.getAllMetadata()->begin() + 267, predicted);
    duplicateFrame.getInputPtr()->insert(duplicateFrame.getInputPtr()->begin() + 267, 9);
    CbrReplayFile duplicateResult(duplicateFrame.getCharacterName(), duplicateFrame.getCharIds());
    auto duplicateError = duplicateResult.makeFullCaseBase(&duplicateFrame, "ny");
    assert(duplicateError.errorCount == 0);
    assert(duplicateError.diagnosticSummary.find("raw_repeats=1") != std::string::npos);
    assert(duplicateError.diagnosticSummary.find("removed=1") != std::string::npos);
    assert(archive(baseResult) == archive(duplicateResult));

    auto gapReplay = recording(false, true, false);
    for (int i = 150; i < gapReplay.MetadataSize(); ++i)
        gapReplay.CopyMetadataPtr(i)->SetFrameCount(i + 3);
    auto gapError = converted.makeFullCaseBase(&gapReplay, "ny");
    assert(gapError.errorCount == 0);
    assert(gapError.diagnosticSummary.find("cleaned_gaps=1") != std::string::npos);

    auto repeated = recording(false, true, false);
    for (auto& metadata : *repeated.getAllMetadata()) metadata->SetFrameCount(0);
    auto invalidTimeline = converted.makeFullCaseBase(&repeated, "ny");
    assert(invalidTimeline.errorDetail.find("[TIMELINE_INVALID]") != std::string::npos);
    assert(invalidTimeline.errorDetail.find("[COMMAND_UNRESOLVED]") == std::string::npos);
    assert(repeated.MetadataSize() == 320);

    auto good = recording(false, true);
    assert(converted.makeFullCaseBase(&good, "ny").errorCount == 0);
    bad.getInputPtr()->pop_back();
    auto invalidCapture = converted.makeFullCaseBase(&bad, "ny");
    assert(invalidCapture.errorCount == 1);
    assert(invalidCapture.errorDetail.find("[CAPTURE_INVALID]") != std::string::npos);
    assert(invalidCapture.errorDetail.find("[COMMAND_UNRESOLVED]") == std::string::npos);
    assert(converted.getCaseBaseLength() == 0);
    auto missingMetadata = recording(false, true, false);
    (*missingMetadata.getAllMetadata())[50].reset();
    assert(converted.makeFullCaseBase(&missingMetadata, "ny").errorDetail.find("[CAPTURE_INVALID]") != std::string::npos);
    assert(converted.makeFullCaseBase(nullptr, "ny").errorCount == 1);
    std::cout << "CBR conversion: rollback equivalence, cancels, both facings, classified diagnostics, preservation, and retries passed\n";
}
