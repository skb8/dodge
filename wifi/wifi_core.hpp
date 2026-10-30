// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <vector>

namespace dodge_wifi {
enum class Security { Open, Psk, Sae, Owe, Unsupported };
struct Network {
    std::string ssid, bssid;
    Security security = Security::Unsupported;
    int signal = -100;
};
std::string DecodeSSID(const std::string& text);
std::string Hex(const std::string& text);
std::string Label(const std::string& text);
std::vector<Network> ParseScan(const std::string& reply);
std::string SecurityName(Security security);
bool ValidPassword(Security security, const std::string& password);
std::vector<std::string> Configure(const Network& network, const std::string& password, int id);
std::string Field(const std::string& status, const std::string& key);
// One request per socket: timed-out replies cannot contaminate the next request.
std::string Request(const std::string& path, const std::string& command, int timeout_ms = 600);
// 204 = reachable, 200/3xx = captive portal, 0 = unverified. No portal login UI.
int CheckInternet(const std::string& dns, const std::string& iface);
}
