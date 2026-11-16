// Top-level game-state machine of the PC Edition:
//   Title ("Press Start Button") -> MainMenu (Start / Continue / Options / Quit)
//   Start    -> SignUp (Digimon Online account) -> PackSelect (original starter packs A/B/C) -> Playing (lobby)
//   Continue -> LogIn (local account) -> Playing (where the player saved)
//   Playing  -> Pause (Esc: Resume / Save / Return to Title / Quit)
// Playing owns a Game (field <-> battle). Accounts go through AccountService (local now, server later);
// the player's data is Party + account + position (one save block, see Story::saveGame).
#pragma once
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "game/account.h"
#include "game/game.h"
#include "game/starter_sets.h"
#include "ui/pack_art.h"

namespace dw3 {

// Test/automation options for the flow (all off for players).
struct FlowOptions {
    std::string stopAt;      // --flow title|menu|options|slots|newslot|signup|pack|newgame|continue
    int slot = 0;            // --slot N (1-based): save slot for --flow newgame / continue (0 = first free / used)
    std::string account;     // --account NAME: pre-filled account name (tests)
    int selectPack = 0;      // --select-set N (1-based): pick this pack automatically
    std::string lobbyStage = "S210";  // Asuka City Main Lobby (reference shot 38)
};

class App {
public:
    enum class State { Title, MainMenu, Options, Slots, SignUp, PackSelect, LogIn, Playing, Pause, Quit };

    bool init(const std::filesystem::path& dataRoot, const std::filesystem::path& stageDir, const DebugOptions& dbg,
              const FlowOptions& flow, std::string& err);
    void update(const Input& in);
    void render(int windowW, int windowH);
    void shutdown();  // frees GL resources (context must be current)

    bool quitRequested() const { return state_ == State::Quit; }
    bool fullscreen() const { return fullscreen_; }
    State state() const { return state_; }

private:
    void go(State s);
    void startNewGame();
    void startContinue();
    bool saveExists() const;  // the selected slot has a save + account

    // Save slots: <save dir>/slotN/ holds story.sav + account.cfg of one Digimon Online account.
    struct SlotInfo {
        bool used = false;
        std::string account, area, lead;  // lead = "Kotemon Lv 3" (or "No partner yet")
        long long seconds = 0;
    };
    std::filesystem::path slotDir(int slot) const;
    SlotInfo readSlot(int slot) const;
    void refreshSlots();
    bool anySlotUsed() const;
    void selectSlot(int slot);  // the game + account service now use this slot's folder
    void loadAreaNames();
    void updateSlots(const Input& in);
    void renderSlots();
    void loadOptions();
    void saveOptions() const;

    void updateTitle(const Input& in);
    void updateMenu(const Input& in);
    void updateOptions(const Input& in);
    void updateSignUp(const Input& in);
    void updatePack(const Input& in);
    void updateLogIn(const Input& in);
    void updatePause(const Input& in);

    void drawTitleBg();
    void drawOnlineBg();
    void drawTerminal(const std::vector<std::string>& lines, bool typewriter);
    void renderTitle();
    void renderMenu();
    void renderOptions();
    void renderSignUp();
    void renderPack();
    void renderLogIn();
    void renderPause();
    void loadRegText();
    void drawMsg(float x, float y, const std::string& ref, uint32_t color);
    void drawMenuWindow(const char* title, const std::vector<std::string>& items, int sel);

    std::filesystem::path root_, stageDir_;
    DebugOptions dbg_;
    FlowOptions flow_;
    State state_ = State::Title;
    long long ticks_ = 0, stateTicks_ = 0;
    std::unique_ptr<Game> game_;
    std::unique_ptr<AccountService> accounts_;
    Account account_;
    std::vector<StarterPack> packs_;
    ui::PackArt titleBg_, logo_, hexBg_, online_;

    // save slots
    std::filesystem::path baseDir_;    // Story::saveDir() at start (options.cfg lives here)
    std::vector<SlotInfo> slots_;
    std::map<std::string, std::string> areaNames_;  // data/areas.cfg
    int slot_ = -1;                    // selected slot (0-based), -1 = none
    int slotSel_ = 0;
    bool slotsForNew_ = false;         // true = New Game picks a slot, false = Continue loads one
    bool overwriteAsk_ = false;        // "Overwrite this save?" YES / NO

    // menus
    std::vector<int> menuItems_;  // 0 New Game, 1 Continue, 2 Options, 3 Quit
    int menuSel_ = 0, optSel_ = 0, pauseSel_ = 0, packSel_ = 0;
    bool fullscreen_ = false;
    std::string pauseMsg_;
    int pauseMsgTicks_ = 0;

    // sign-up
    std::string name_, pass_;
    int field_ = 0;            // 0 name, 1 password
    int gridX_ = 0, gridY_ = 0;
    std::string signMsg_;
    bool signError_ = false;
    int doneTicks_ = -1;       // >= 0: "downloading / logging in" countdown before the next screen
    bool prevUp_ = false, prevDown_ = false, prevLeft_ = false, prevRight_ = false;
    std::vector<std::string> term_;  // current terminal lines
    long long termStart_ = 0;
    std::string error_;
    std::vector<std::vector<uint8_t>> regText_;  // USNAMEET messages (raw encoded)
    bool confirm_ = false, yes_ = true;          // pack: "Register account? YES / NO"
};

}  // namespace dw3
