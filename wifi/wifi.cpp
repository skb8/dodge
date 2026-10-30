// SPDX-License-Identifier: GPL-3.0-or-later
#include "wifi.hpp"
#include "wifi_core.hpp"
#include "../data.hpp"
#include "../gui/pages.hpp"
#include "../gui/gui.hpp"
#include <cutils/properties.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/wait.h>
#include <cstdio>
#include <cerrno>
#include <cstdlib>

namespace dodge_wifi {
namespace {
using Clock = std::chrono::steady_clock;
const char* kControl = "/tmp/dodge-wifi/ctrl/wlan0";
std::string Property(const char* name) {
    char value[PROPERTY_VALUE_MAX]{}; property_get(name, value, ""); return value;
}
struct Command { std::string action, password; Network network; };
struct Snapshot {
    std::vector<Network> networks;
    std::string state = "off", ssid, ip;
    bool enabled = false;
};
// Detached worker lives for the lifetime of recovery. Heap allocation avoids shutdown races.
class Wifi {
public:
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Command> commands;
    Snapshot snapshot;
    Network selected;
    std::vector<Network> displayed;
    Wifi() { std::thread([this] { Run(); }).detach(); }
    void Enqueue(Command cmd) {
        std::lock_guard<std::mutex> lock(mutex);
        // Keep secrets out of queues on repeated taps; bound pending work.
        if (commands.size() >= 4) return;
        commands.push_back(std::move(cmd)); wake.notify_one();
    }
private:
    int events = -1;
    int id = -1;
    unsigned session = 0;
    bool enabled = false, dhcp = false, connecting = false, failed = false;
    Clock::time_point started{}, last_scan{}, last_check{};
    std::string previous_bssid;
    Snapshot current;
    std::string Request(const std::string& cmd) { return dodge_wifi::Request(kControl, cmd); }
    bool Ok(const std::string& cmd) { return Request(cmd) == "OK\n"; }
    void Publish() { std::lock_guard<std::mutex> lock(mutex); snapshot = current; }
    void CloseEvents() { if (events >= 0) close(events); events = -1; }
    void Attach() {
        if (events >= 0) return;
        events = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
        if (events < 0) return;
        sockaddr_un local{}, remote{}; local.sun_family = remote.sun_family = AF_UNIX;
        snprintf(local.sun_path + 1, sizeof(local.sun_path) - 1, "dodge-wifi-events-%d", getpid());
        snprintf(remote.sun_path, sizeof(remote.sun_path), "%s", kControl);
        if (::bind(events, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0 ||
            connect(events, reinterpret_cast<sockaddr*>(&remote), sizeof(remote)) < 0 ||
            send(events, "ATTACH", 6, 0) != 6) CloseEvents();
    }
    void StopDhcp() {
        property_set("sys.dodge.wifi.session", std::to_string(++session).c_str());
        property_set("ctl.stop", "dodge_wifi_dhcp");
        property_set("sys.dodge.wifi.lease", ""); property_set("sys.dodge.wifi.dns", "");
        pid_t child = fork();
        if (child == 0) { execl("/system/bin/sh", "sh", "/system/bin/dodge-wifi-clear", nullptr); _exit(127); }
        if (child > 0) { int status; while (waitpid(child, &status, 0) < 0 && errno == EINTR) {} }
        dhcp = false;
        current.ip.clear(); previous_bssid.clear(); last_check = Clock::time_point{};
    }
    void Disconnect() {
        Ok("DISCONNECT"); Ok("REMOVE_NETWORK all"); id = -1;
        StopDhcp(); connecting = false; failed = false;
        current.ssid.clear(); current.state = "disconnected";
    }
    void Handle(Command& cmd) {
        if (cmd.action == "enable") {
            if (Property("sys.dodge.wifi.driver_ready") != "1") {
                current.state = "unavailable"; Publish(); return;
            }
            enabled = current.enabled = true; current.state = "starting"; Publish();
            property_set("ctl.start", "dodge_wifi_supplicant");
            started = Clock::now(); last_scan = Clock::time_point{};
            current.state = "starting"; failed = false;
        } else if (cmd.action == "disable") {
            Disconnect(); CloseEvents(); property_set("ctl.stop", "dodge_wifi_supplicant");
            enabled = current.enabled = false; current.networks.clear(); current.state = "off";
        } else if (enabled && cmd.action == "disconnect") Disconnect();
        else if (enabled && cmd.action == "scan") {
            if (Ok("SCAN")) { last_scan = Clock::now(); if (!connecting && current.ip.empty()) current.state = "scanning"; }
        } else if (enabled && cmd.action == "connect") {
            if (!ValidPassword(cmd.network.security, cmd.password)) { current.state = "invalid_password"; return; }
            Disconnect();
            std::string reply = Request("ADD_NETWORK");
            char* end = nullptr; long number = strtol(reply.c_str(), &end, 10);
            if (reply.empty() || end == reply.c_str() || (*end != '\n' && *end != '\0') || number < 0 || number > 100000) {
                current.state = "supplicant_error"; return;
            }
            id = number;
            auto configuration = Configure(cmd.network, cmd.password, id);
            bool success = !configuration.empty();
            for (auto& item : configuration) {
                if (success && !Ok(item)) success = false;
                std::fill(item.begin(), item.end(), '\0');
            }
            std::fill(cmd.password.begin(), cmd.password.end(), '\0'); cmd.password.clear();
            if (!success) { Disconnect(); current.state = "supplicant_error"; return; }
            current.ssid = Label(cmd.network.ssid); current.state = "associating";
            connecting = true; started = Clock::now(); failed = false;
        }
    }
    void Poll() {
        if (!enabled) return;
        std::string status = Request("STATUS");
        if (status.empty() || status.find("wpa_state=") == std::string::npos) {
            if (Clock::now() - started > std::chrono::seconds(15)) {
                current.state = "unavailable"; current.ip.clear(); CloseEvents();
                if (dhcp) StopDhcp();
            }
            return;
        }
        Attach();
        if (events >= 0) {
            char buffer[2048]; ssize_t bytes;
            while ((bytes = recv(events, buffer, sizeof(buffer), 0)) > 0) {
                std::string event(buffer, bytes);
                if (event.find("CTRL-EVENT-SSID-TEMP-DISABLED") != std::string::npos &&
                    (event.find("WRONG_KEY") != std::string::npos || event.find("AUTH_FAILED") != std::string::npos)) {
                    Disconnect(); failed = true; current.state = "authentication_failed";
                }
                if (event.find("CTRL-EVENT-TERMINATING") != std::string::npos) CloseEvents();
            }
        }
        if (failed) return;
        std::string state = Field(status, "wpa_state");
        if (state == "COMPLETED") {
            current.ssid = Label(DecodeSSID(Field(status, "ssid")));
            std::string bssid = Field(status, "bssid");
            if (!dhcp || bssid != previous_bssid) {
                StopDhcp(); previous_bssid = bssid;
                property_set("ctl.start", "dodge_wifi_dhcp");
                dhcp = true; started = Clock::now(); current.state = "obtaining_ip";
            }
            std::string lease = Property("sys.dodge.wifi.lease");
            if (!lease.empty()) {
                if (current.ip != lease) last_check = Clock::time_point{};
                current.ip = lease; connecting = false;
                if (last_check == Clock::time_point{} || Clock::now() - last_check > std::chrono::seconds(30)) {
                    current.state = "checking"; Publish();
                    int code = CheckInternet(Property("sys.dodge.wifi.dns"), "wlan0");
                    current.state = code == 204 ? "online" : (code == 200 || (code >= 300 && code < 400)) ? "captive_portal" : "unverified";
                    last_check = Clock::now();
                }
            } else {
                current.ip.clear();
                current.state = Clock::now() - started > std::chrono::seconds(30) ? "dhcp_failed" : "obtaining_ip";
            }
        } else {
            if (dhcp) { StopDhcp(); started = Clock::now(); connecting = id >= 0; }
            if (connecting && Clock::now() - started > std::chrono::seconds(40)) {
                Disconnect(); failed = true; current.state = "connection_timeout";
            } else if (connecting) current.state = "associating";
            else if (current.state != "invalid_password" && current.state != "supplicant_error") current.state = "disconnected";
        }
        if (Clock::now() - last_scan > std::chrono::seconds(15) && !connecting) {
            Ok("SCAN"); last_scan = Clock::now();
        }
        auto scan = Request("SCAN_RESULTS");
        if (scan.compare(0, 5, "bssid") == 0) current.networks = ParseScan(scan);
    }
    void Run() {
        for (;;) {
            std::deque<Command> todo;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait_for(lock, std::chrono::milliseconds(500), [this] { return !commands.empty(); });
                todo.swap(commands);
            }
            for (auto& cmd : todo) Handle(cmd);
            Poll(); Publish();
        }
    }
};
Wifi& Get() { static Wifi* instance = new Wifi; return *instance; }
void Set(const std::string& name, const std::string& value) {
    if (DataManager::GetStrValue(name) != value) DataManager::SetValue(name, value, 0);
}
std::string Text(const std::string& state) {
    const std::string fallback = state == "off" ? "Wi-Fi off" : state == "online" ? "Internet available" :
        state == "unverified" ? "Connected; internet unverified" : state == "captive_portal" ? "Sign-in required" :
        state == "starting" ? "Starting Wi-Fi" : state == "scanning" ? "Scanning" :
        state == "associating" ? "Connecting" : state == "obtaining_ip" ? "Obtaining IP address" :
        state == "checking" ? "Checking internet" : state == "authentication_failed" ? "Authentication failed; check password" :
        state == "invalid_password" ? "Invalid password" : state == "dhcp_failed" ? "Could not obtain IP address" :
        state == "connection_timeout" ? "Connection timed out" : state == "unavailable" ? "Wi-Fi driver or supplicant unavailable" :
        state == "supplicant_error" ? "Connection request failed" : "Disconnected";
    return gui_parse_text("{@wifi_" + state + "=" + fallback + "}");
}
}
void Pump() {
    static auto last = Clock::time_point{};
    if (Clock::now() - last < std::chrono::milliseconds(500)) return;
    last = Clock::now();
    if (PageManager::GetCurrentPage() != "dodge_wifi_connect") {
        Set("dodge_wifi_password", ""); Set("dodge_wifi_password_mask", "");
    }
    if (PageManager::GetCurrentPage() == "dodge_wifi_connect") {
        Network selected;
        { std::lock_guard<std::mutex> lock(Get().mutex); selected = Get().selected; }
        bool valid = ValidPassword(selected.security, DataManager::GetStrValue("dodge_wifi_password"));
        Set("dodge_wifi_valid_password", valid ? "1" : "0");
        if (valid) Set("dodge_wifi_input_error", "");
    }
    Snapshot s;
    { std::lock_guard<std::mutex> lock(Get().mutex); s = Get().snapshot; }
    Set("dodge_wifi_enabled", s.enabled ? "1" : "0");
    Set("dodge_wifi_state", s.state); Set("dodge_wifi_status", Text(s.state));
    Set("dodge_wifi_ip", s.ip); Set("dodge_wifi_ssid", s.ssid);
    Set("dodge_wifi_summary", Text(s.state) + (s.ssid.empty() ? "" : " · " + s.ssid));
    // Labels are reparsed only when their generation changes; a tap freezes the selected Network.
    bool changed = false;
    for (size_t i = 0; i < 32; ++i) {
        std::string value;
        if (i < s.networks.size()) {
            const auto& n = s.networks[i];
            value = Label(n.ssid) + " · " + SecurityName(n.security) + " · " + std::to_string(n.signal) + " dBm";
        }
        std::string key = "dodge_wifi_row_" + std::to_string(i);
        if (DataManager::GetStrValue(key) != value) { Set(key, value); changed = true; }
    }
    { std::lock_guard<std::mutex> lock(Get().mutex); Get().displayed = s.networks; }
    Set("dodge_wifi_count", std::to_string(s.networks.size()));
    if (changed) {
        static unsigned generation = 0;
        Set("dodge_wifi_generation", std::to_string(++generation));
    }
}
void Action(const std::string& action) {
    if (action.compare(0, 7, "select:") == 0) {
        char* end = nullptr; long index = strtol(action.c_str()+7, &end, 10);
        Network selected;
        {
            std::lock_guard<std::mutex> lock(Get().mutex);
            if (!end || *end || index < 0 || static_cast<size_t>(index) >= Get().displayed.size()) return;
            selected = Get().displayed[index]; Get().selected = selected;
        }
        Set("dodge_wifi_password", ""); Set("dodge_wifi_password_mask", "");
        Set("dodge_wifi_show_password", "0");
        Set("dodge_wifi_valid_password", "0");
        Set("dodge_wifi_input_error", "");
        Set("dodge_wifi_selected", Label(selected.ssid));
        Set("dodge_wifi_security", SecurityName(selected.security));
        Set("dodge_wifi_need_password", selected.security == Security::Psk || selected.security == Security::Sae ? "1" : "0");
        Set("dodge_wifi_supported", selected.security == Security::Unsupported ? "0" : "1");
        if (selected.security == Security::Open || selected.security == Security::Owe) {
            Get().Enqueue({"connect", {}, selected});
        } else {
            PageManager::ChangePage(selected.security == Security::Unsupported ? "dodge_wifi_unsupported" : "dodge_wifi_connect");
        }
    } else if (action == "connect") {
        Command cmd; cmd.action = action;
        { std::lock_guard<std::mutex> lock(Get().mutex); cmd.network = Get().selected; }
        cmd.password = DataManager::GetStrValue("dodge_wifi_password");
        if (!ValidPassword(cmd.network.security, cmd.password)) {
            std::fill(cmd.password.begin(), cmd.password.end(), '\0');
            Set("dodge_wifi_input_error", Text("invalid_password"));
            return;
        }
        Set("dodge_wifi_password", ""); Set("dodge_wifi_password_mask", "");
        Get().Enqueue(std::move(cmd)); PageManager::ChangePage("dodge_wifi");
    } else if (action == "cancel") {
        Set("dodge_wifi_password", ""); Set("dodge_wifi_password_mask", "");
        PageManager::ChangePage("dodge_wifi");
    } else Get().Enqueue({action, {}, {}});
}
}
