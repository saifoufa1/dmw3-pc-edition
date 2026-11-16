#include "game/account.h"

#include <cctype>
#include <cstdio>
#include <fstream>

namespace dw3 {

namespace {
std::string hashPassword(const std::string& name, const std::string& pw) {
    uint64_t h = 1469598103934665603ull;
    for (char c : "dmw3pc:" + name + ":" + pw) {
        h ^= uint8_t(c);
        h *= 1099511628211ull;
    }
    char b[24];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(h));
    return b;
}
}  // namespace

const char* accountResultText(AccountResult r) {
    switch (r) {
        case AccountResult::Ok: return "OK";
        case AccountResult::BadName: return "Use 1-12 letters or digits.";
        case AccountResult::BadPassword: return "Password: up to 16 characters.";
        case AccountResult::NameTaken: return "That account already exists.";
        case AccountResult::NoAccount: return "No account found.";
        case AccountResult::WrongPassword: return "Wrong password.";
        case AccountResult::StorageError: return "Could not save the account.";
    }
    return "?";
}

bool AccountService::validName(const std::string& n) {
    if (n.empty() || n.size() > 12 || n.front() == ' ' || n.back() == ' ') return false;
    for (char c : n)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != ' ' && c != '-') return false;
    return true;
}

bool LocalAccountService::read(std::string& name, std::string& hash) const {
    std::ifstream f(file());
    std::string line;
    if (!f || !std::getline(f, line) || line.rfind("dmw3-account 1", 0) != 0) return false;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("name ", 0) == 0) name = line.substr(5);
        else if (line.rfind("pass ", 0) == 0) hash = line.substr(5);
    }
    return !name.empty();
}

AccountResult LocalAccountService::signUp(const std::string& name, const std::string& pw, Account& out) {
    if (!validName(name)) return AccountResult::BadName;
    if (pw.size() > 16) return AccountResult::BadPassword;
    // One local account per machine: a new sign-up replaces it (New Game).
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
    std::ofstream f(file());
    if (!f) return AccountResult::StorageError;
    f << "dmw3-account 1\nname " << name << "\npass " << hashPassword(name, pw) << "\n";
    if (!f) return AccountResult::StorageError;
    out.name = name;
    return AccountResult::Ok;
}

AccountResult LocalAccountService::logIn(const std::string& name, const std::string& pw, Account& out) {
    std::string n, h;
    if (!read(n, h) || n != name) return AccountResult::NoAccount;
    if (h != hashPassword(name, pw)) return AccountResult::WrongPassword;
    out.name = n;
    return AccountResult::Ok;
}

std::string LocalAccountService::rememberedAccount() const {
    std::string n, h;
    return read(n, h) ? n : std::string();
}

AccountResult LocalAccountService::resume(Account& out) {
    std::string n, h;
    if (!read(n, h)) return AccountResult::NoAccount;
    out.name = n;
    return AccountResult::Ok;
}

}  // namespace dw3
