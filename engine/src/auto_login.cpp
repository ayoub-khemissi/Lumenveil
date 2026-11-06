#include "auto_login.h"

#include "client_code.h"
#include "log.h"

#include <windows.h>

#include <cstdint>
#include <string>

namespace
{
constexpr const char* kVariable = "LORDAERON_AUTOLOGIN";
constexpr uintptr_t kFrameScriptExecute = 0x00819210;
constexpr uintptr_t kLuaState = 0x00D3F78C;
constexpr ClientCode kFrameScriptExecuteCode = {kFrameScriptExecute, 48, 0x78555216c1e95cc8ull};
constexpr ULONGLONG kLifetimeMs = 60000;
constexpr size_t kMaxAccount = 32;
constexpr size_t kMinTicket = 8;
constexpr size_t kMaxTicket = 32;

using ExecuteFn = void(__cdecl*)(const char*, const char*, int);

std::string g_script;
ULONGLONG g_since = 0;
bool g_checked = false;
bool g_usable = false;

bool Only(const std::string& text, bool digits)
{
    for (char c : text)
        if (!((c >= 'A' && c <= 'Z') || (digits && c >= '0' && c <= '9')))
            return false;
    return true;
}

void Forget()
{
    if (!g_script.empty())
        SecureZeroMemory(&g_script[0], g_script.size());
    g_script.clear();
}
}

void AutoLoginAttach()
{
    char buffer[128] = {};
    const DWORD length = GetEnvironmentVariableA(kVariable, buffer, sizeof(buffer));
    SetEnvironmentVariableA(kVariable, nullptr);
    if (length == 0 || length >= sizeof(buffer))
        return;
    std::string value(buffer, length);
    SecureZeroMemory(buffer, sizeof(buffer));
    const size_t tab = value.find('\t');
    std::string account = tab == std::string::npos ? std::string() : value.substr(0, tab);
    std::string ticket = tab == std::string::npos ? std::string() : value.substr(tab + 1);
    SecureZeroMemory(&value[0], value.size());
    if (account.empty() || account.size() > kMaxAccount || !Only(account, true) || ticket.size() < kMinTicket ||
        ticket.size() > kMaxTicket || !Only(ticket, false))
    {
        VF_LOG_INFO("auto login: the launcher's ticket is not in the expected form; the login screen is left to the player");
        return;
    }
    g_script = "if not LORDAERON_AUTOLOGIN and AccountLoginAccountEdit and AccountLoginAccountEdit:IsVisible() and "
               "DefaultServerLogin then LORDAERON_AUTOLOGIN = 1 AccountLoginAccountEdit:SetText(\"" +
               account + "\") DefaultServerLogin(\"" + account + "\", \"" + ticket + "\") end";
    SecureZeroMemory(&ticket[0], ticket.size());
    g_since = GetTickCount64();
    VF_LOG_INFO("auto login: a launcher ticket for %s waits for the login screen", account.c_str());
}

bool AutoLoginPending()
{
    return !g_script.empty();
}

std::string AutoLoginScriptForTest()
{
    return g_script;
}

void AutoLoginFrame()
{
    if (g_script.empty())
        return;
    if (GetTickCount64() - g_since > kLifetimeMs)
    {
        Forget();
        VF_LOG_INFO("auto login: the ticket's minute is over");
        return;
    }
    if (!g_checked)
    {
        g_checked = true;
        g_usable = ClientCodeMatches(kFrameScriptExecuteCode);
        if (!g_usable)
            VF_LOG_INFO("auto login: the client's script runner is not the 3.3.5a (12340) one; the login screen is left "
                        "to the player");
    }
    if (!g_usable)
    {
        Forget();
        return;
    }
    if (*reinterpret_cast<void* const*>(kLuaState) == nullptr)
        return;
    reinterpret_cast<ExecuteFn>(kFrameScriptExecute)(g_script.c_str(), "Lordaeron", 0);
}
