// Original game text, read at runtime from the player's own disc data (never copied into the repo).
// A message reference ("ref") names one message:
//   dmg200:4:3   event bank DAT/COUNTRY/USA/USDMG200.BIN, block 4, message 3
//   talk00:0x13  NPC talk bank DAT/COUNTRY/USA/USTALK00.BIN, message 0x13
#pragma once
#include <filesystem>
#include <map>
#include <string>

#include "core/bytes.h"
#include "formats/dw3_text.h"

namespace dw3 {

class TextDb {
public:
    void setRoot(std::filesystem::path root, std::string country = "USA");
    void setPlayerName(std::string n) { player_ = std::move(n); }
    const std::string& playerName() const { return player_; }
    // Resolves a ref; false if the bank/message is missing or does not decode cleanly.
    bool get(const std::string& ref, Message& out);
    static bool isRef(const std::string& s);  // looks like "dmg###:b:m" or "talk##:id"

private:
    struct Bank {
        Bytes data;
        EventBank ev;
        TalkBank talk;
        bool tried = false, ok = false;
    };
    Bank& bank(const std::string& name);  // "DMG200" / "TALK00"
    std::filesystem::path root_;
    std::string country_ = "USA", prefix_ = "US";
    std::string player_ = "Junpei";
    std::map<std::string, Bank> banks_;
};

}  // namespace dw3
