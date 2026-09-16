// Serialization-only harness: real production class layouts/serializers with
// minimal constructors/accessors, without linking game hooks or AI algorithms.
#include "CbrFileIO.h"
#include "CbrIdentity.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>

CbrData::CbrData() {}
CbrReplayFile::CbrReplayFile() { characterId = {}; }
CbrCase::CbrCase() {}
Metadata::Metadata() {}
Helper::Helper() {}
AnnotatedReplay::AnnotatedReplay() { replayIndex = 0; }
std::string CbrData::getPlayerName() { return playerName; }
std::string CbrData::getCharName() { return characterName; }
int CbrData::getReplayCount() { return static_cast<int>(replayFiles.size()); }
void CbrData::setPlayerName(std::string s) { playerName = s; }
void CbrData::setCharName(std::string s) { characterName = s; }
std::vector<CbrReplayFile>* CbrData::getReplayFiles() { return &replayFiles; }
void CbrReplayFile::CopyInput(std::vector<int> v) { input = v; }
int CbrReplayFile::getInput(int i) { return input.at(i); }
std::string AnnotatedReplay::getPlayerName() { return playerName; }
std::vector<int> AnnotatedReplay::getInput() { return input; }
#ifdef CBR_LEGACY
CbrData::CbrData(std::string n, std::string c, int i) : playerName(n), characterName(c), characterIndex(i) {}
AnnotatedReplay::AnnotatedReplay(std::string n, std::string p1, std::string p2, int c1, int c2)
    : playerName(n), characterName{p1, p2}, characterId{c1, c2}, replayIndex(0) {}
#else
CbrData::CbrData(std::string n, std::string c, int i, uint64_t id)
    : playerName(n), characterName(c), characterIndex(i), steamId(id) {}
AnnotatedReplay::AnnotatedReplay(std::string n, std::string p1, std::string p2, int c1, int c2, uint64_t id)
    : playerName(n), characterName{p1, p2}, characterId{c1, c2}, replayIndex(0), steamId(id) {}
#endif

// A real historical archive prefix, not a new wrapper class (which would add
// extra Boost class metadata and test a different wire format).
static void writePrefixed(const std::string& path, CbrData& data)
{
    std::ofstream output(path, std::ios::binary);
    boost::iostreams::filtering_ostream compressed;
    compressed.push(boost::iostreams::gzip_compressor());
    compressed.push(output);
    boost::archive::binary_oarchive archive(compressed);
    std::string character = data.getCharName(), name = data.getPlayerName(), opponents = "jn(1)";
    int count = data.getReplayCount();
    archive << character << name << opponents << count << data;
}

template<class T> static T readArchive(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    boost::iostreams::filtering_istream compressed;
    compressed.push(boost::iostreams::gzip_decompressor());
    compressed.push(input);
    boost::archive::binary_iarchive archive(compressed);
    T data;
    archive >> data;
    return data;
}

struct FailedWrite {
    template<class Archive> void serialize(Archive&, unsigned) {
        throw std::runtime_error("Injected serialization failure");
    }
};

int main(int argc, char** argv)
{
    assert(argc == 2);
    std::filesystem::current_path(argv[1]);
#ifdef CBR_LEGACY
    const std::string prefix = "v" + std::to_string(CBR_LEGACY);
    CbrData old("Original name", "rg", 0);
    CbrReplayFile replay;
    replay.CopyInput({5, 6, 16, 32});
    old.getReplayFiles()->push_back(replay);
    WriteCbrArchiveAtomic(prefix + ".cbr", old);
    writePrefixed(prefix + "-prefixed.cbr", old);
    AnnotatedReplay recording("Original name", "rg", "jn", 0, 1);
    WriteCbrArchiveAtomic(prefix + ".rev", recording);
    FileMetadata metadata;
    std::strcpy(metadata.charName, "rg");
    std::strcpy(metadata.playerName, "Original name");
    std::strcpy(metadata.opponentChar, "jn(1)");
    metadata.rCount = 1;
    WriteCbrArchiveAtomic(prefix + ".met", metadata);
#else
    for (const std::string prefix : {"v0", "v3"}) {
        for (const std::string suffix : {".cbr", "-prefixed.cbr"}) {
            auto old = ReadCbrDataArchive(prefix + suffix);
            assert(old.getSteamId() == 0 && old.getPlayerName() == "Original name");
            assert(old.getCharName() == "rg" && old.getReplayCount() == 1);
            assert(old.getReplayFiles()->at(0).getInput(2) == 16);
        }
        auto oldReplay = readArchive<AnnotatedReplay>(prefix + ".rev");
        assert(oldReplay.getSteamId() == 0 && oldReplay.getPlayerName() == "Original name");
        auto oldMetadata = readArchive<FileMetadata>(prefix + ".met");
        assert(oldMetadata.steamId == 0 && oldMetadata.displayName == "Original name");
        assert(oldMetadata.rCount == 1);
    }
    const uint64_t a = 76561198159235859ULL, b = 76561199061467342ULL;
    assert(CbrFileStem("rg", "Old", a) == CbrFileStem("rg", "New / name", a));
    assert(CbrFileStem("rg", "Same", a) != CbrFileStem("rg", "Same", b));
    assert(CbrFileStem("rg", "Same", 0) == "rgSame");
    assert(!SameCbrIdentity(a, "Same", b, "Same"));
    assert(!SameCbrIdentity(a, "Same", 0, "Same"));
    assert(SameCbrIdentity(a, "Old", a, "New"));
    assert(!SameCbrIdentity(0, "Old", 0, "New"));
    const auto path = CbrFileStem("rg", "Old", a) + ".cbr";
    auto data = ReadCbrDataArchive("v3.cbr");
    data.setSteamId(a);
    data.setPlayerName("Old");
    WriteCbrArchiveAtomic(path, data);
    auto renamed = ReadCbrDataArchive(path);
    renamed.setPlayerName("New / name");
    renamed.getReplayFiles()->push_back(renamed.getReplayFiles()->front());
    WriteCbrArchiveAtomic(CbrFileStem("rg", renamed.getPlayerName(), a) + ".cbr", renamed);
    auto loaded = ReadCbrDataArchive(path);
    assert(loaded.getSteamId() == a && loaded.getPlayerName() == "New / name");
    assert(loaded.getReplayCount() == 2 && loaded.getReplayFiles()->at(1).getInput(3) == 32);
    // Same name, another account: independent destination and contents.
    CbrData other("New / name", "rg", 0, b);
    const auto otherPath = CbrFileStem("rg", other.getPlayerName(), b) + ".cbr";
    WriteCbrArchiveAtomic(otherPath, other);
    assert(ReadCbrDataArchive(otherPath).getSteamId() == b);
    assert(ReadCbrDataArchive(path).getReplayCount() == 2);
    // A failed serialization must leave the last successful archive readable.
    bool failed = false;
    try { WriteCbrArchiveAtomic(path, FailedWrite{}); }
    catch (const std::exception&) { failed = true; }
    assert(failed && ReadCbrDataArchive(path).getReplayCount() == 2);
    assert(!std::filesystem::exists(path + ".tmp"));
    // Exercise replacement failure too, not only a serializer exception.
    std::filesystem::create_directory("blocked.cbr");
    std::ofstream("blocked.cbr/keep") << "preserved";
    failed = false;
    try { WriteCbrArchiveAtomic("blocked.cbr", data); }
    catch (const std::exception&) { failed = true; }
    assert(failed && std::filesystem::exists("blocked.cbr/keep"));
    assert(!std::filesystem::exists("blocked.cbr.tmp"));
    FileMetadata meta;
    meta.steamId = a;
    meta.displayName = std::string(150, 'x') + " / full display name";
    meta.rCount = 2;
    WriteCbrArchiveAtomic("new.met", meta);
    const auto fullMeta = readArchive<FileMetadata>("new.met");
    assert(fullMeta.steamId == a && fullMeta.displayName == meta.displayName);
    AnnotatedReplay round("Rain", "rg", "jn", 0, 1, a);
    auto buffered = std::move(round);
    WriteCbrArchiveAtomic("new.rev", buffered);
    assert(readArchive<AnnotatedReplay>("new.rev").getSteamId() == a);
    std::cout << "PASS: old v0/v3 archives and metadata, both historical layouts, full IDs, rename continuity, duplicate names, replay snapshots, and failed-save preservation\n";
#endif
}
