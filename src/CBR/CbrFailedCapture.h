#pragma once
#include "AnnotatedReplay.h"
#include "CbrArchive.h"
#include <boost/archive/binary_iarchive.hpp>
#include <boost/filesystem.hpp>

// Separate diagnostic format: existing .cbr/.rev serializers stay unchanged.
// Metadata's historical archive omits the game frame counter, opponentId and
// hitMinY. Preserve those explicitly so rollback can be reproduced on reload.
struct CbrFailedCapture {
    unsigned format = 1;
    int slot = 0;
    std::string build;
    std::string diagnostic;
    AnnotatedReplay replay;
    std::vector<int> frames, opponentIds, hitMinYs;

    template<class Archive> void serialize(Archive& archive, unsigned) {
        archive & format & slot & build & diagnostic & replay & frames & opponentIds & hitMinYs;
    }

    void restoreMetadata() {
        auto& metadata = *replay.getAllMetadata();
        if (format != 1 || frames.size() != metadata.size() ||
            opponentIds.size() != metadata.size() || hitMinYs.size() != metadata.size())
            throw std::runtime_error("Invalid CBR failed-capture metadata");
        for (size_t i = 0; i < metadata.size(); ++i) {
            if (!metadata[i]) continue;
            metadata[i]->frame_count_minus_1 = frames[i];
            metadata[i]->opponentId = opponentIds[i];
            metadata[i]->hitMinY = hitMinYs[i];
        }
    }
};

inline std::string PreserveCbrFailedCapture(AnnotatedReplay& replay, int slot,
    const std::string& diagnostic, const std::string& build,
    const boost::filesystem::path& root = boost::filesystem::path("CBRsave") / "FailedRecordings")
{
    CbrFailedCapture capture;
    capture.slot = slot;
    capture.build = build;
    capture.diagnostic = diagnostic;
    capture.replay = replay;
    for (const auto& metadata : *replay.getAllMetadata()) {
        capture.frames.push_back(metadata ? metadata->frame_count_minus_1 : 0);
        capture.opponentIds.push_back(metadata ? metadata->opponentId : 0);
        capture.hitMinYs.push_back(metadata ? metadata->hitMinY : -1);
    }
    boost::filesystem::create_directories(root);
    // Claim a unique directory, including across launches or simultaneous games.
    boost::filesystem::path directory;
    do { directory = root / boost::filesystem::unique_path("failure-%%%%-%%%%-%%%%-%%%%"); }
    while (!boost::filesystem::create_directory(directory));
    const auto path = (directory / "capture.cbrfailed").string();
    try { WriteCbrArchiveAtomic(path, capture); }
    catch (...) { boost::filesystem::remove(directory); throw; }
    return path;
}

inline CbrFailedCapture ReadCbrFailedCapture(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read CBR failed capture: " + path);
    boost::iostreams::filtering_istream compressed;
    compressed.push(boost::iostreams::gzip_decompressor());
    compressed.push(input);
    boost::archive::binary_iarchive archive(compressed);
    CbrFailedCapture capture;
    archive >> capture;
    capture.restoreMetadata();
    return capture;
}
