// dw3check: original NPC / door extraction (see docs/FORMATS.md "Event actors" and "Area links").
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace dw3 {

// --map-file-ids DISC(.cue|.bin) OUT.cfg : file id -> extracted file name (needs the player's raw disc image once)
int mapFileIds(const std::filesystem::path& root, const std::filesystem::path& disc, const std::filesystem::path& out);
// --dump-npcs S210 : actors (sprite, position, show conditions, talk ids) and area links of one stage
int dumpNpcs(const std::filesystem::path& root, const std::filesystem::path& exeDir, const std::string& stage);
// --extract-npcs OUTDIR [S210 ...] : writes OUTDIR/npcs/S###.npc + OUTDIR/doors/S###.door (all stages if none given)
int extractNpcs(const std::filesystem::path& root, const std::filesystem::path& exeDir, const std::filesystem::path& outDir,
                const std::vector<std::string>& stages);
// corpus check: FIELDSTG tables load, every overlay's area links parse; returns failures
int checkStageLinks(const std::filesystem::path& root, const std::filesystem::path& exeDir);

}  // namespace dw3
