// Digimon Online account (sign-up + log-in). The UI only talks to AccountService, so the local implementation
// used now can be replaced by a server-backed one (OnlineAccountService, later) without touching the screens.
#pragma once
#include <filesystem>
#include <memory>
#include <string>

namespace dw3 {

struct Account {
    std::string name;  // Digimon Online account / tamer name shown in game
};

enum class AccountResult { Ok, BadName, BadPassword, NameTaken, NoAccount, WrongPassword, StorageError };
const char* accountResultText(AccountResult r);

class AccountService {
public:
    virtual ~AccountService() = default;
    // Creates the account and logs it in.
    virtual AccountResult signUp(const std::string& name, const std::string& password, Account& out) = 0;
    virtual AccountResult logIn(const std::string& name, const std::string& password, Account& out) = 0;
    // Name of the account remembered on this machine ("" = none): Continue logs in with it.
    virtual std::string rememberedAccount() const = 0;
    // True when logging in with the remembered account needs no password prompt (local accounts).
    virtual bool canResume() const = 0;
    virtual AccountResult resume(Account& out) = 0;

    static bool validName(const std::string& name);  // 1..12 chars, letters/digits/space/-
};

// Local accounts: one text file per machine (dir/account.cfg). The password is stored as a salted FNV-1a
// hash: this is NOT security, it only keeps the field ready for the server version.
class LocalAccountService : public AccountService {
public:
    explicit LocalAccountService(std::filesystem::path dir) : dir_(std::move(dir)) {}
    AccountResult signUp(const std::string& name, const std::string& password, Account& out) override;
    AccountResult logIn(const std::string& name, const std::string& password, Account& out) override;
    std::string rememberedAccount() const override;
    bool canResume() const override { return !rememberedAccount().empty(); }
    AccountResult resume(Account& out) override;

private:
    std::filesystem::path file() const { return dir_ / "account.cfg"; }
    bool read(std::string& name, std::string& hash) const;
    std::filesystem::path dir_;
};

}  // namespace dw3
