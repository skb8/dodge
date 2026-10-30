// SPDX-License-Identifier: GPL-3.0-or-later
#include "../wifi/wifi_core.hpp"
#include <cassert>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
using namespace dodge_wifi;
int main() {
    assert(DecodeSSID("Cafe\\x20WiFi") == "Cafe WiFi");
    assert(DecodeSSID("slash\\\\tab\\t") == "slash\\tab\t");
    assert(Hex(std::string("a\0b", 3)) == "610062");
    assert(Label("%tw_password%\n{@x}") == "?tw_password???@x?");
    const std::string scan =
        "bssid / frequency / signal level / flags / ssid\n"
        "00:11:22:33:44:55\t2412\t-80\t[WPA2-PSK-CCMP][ESS]\tCafe\\x20WiFi\n"
        "00:11:22:33:44:66\t5180\t-40\t[WPA2-PSK-CCMP][ESS]\tCafe WiFi\n"
        "00:11:22:33:44:77\t5180\t-50\t[ESS]\tCafe WiFi\n"
        "00:11:22:33:44:88\t5180\t-55\t[WPA2-PSK+SAE-CCMP][ESS]\tModern\n"
        "00:11:22:33:44:99\t5180\t-60\t[WPA2-EAP-CCMP][ESS]\tEnterprise\n"
        "00:11:22:33:44:aa\t5180\t-60\t[WEP][ESS]\tOld\n"
        "00:11:22:33:44:bb\t5180\t-60\t[WAPI][ESS]\tUnknown\n"
        "00:11:22:33:44:cc\t5180\t-60\t[WPA2-OWE-CCMP][ESS]\tEnhanced\n"
        "00:11:22:33:44:dd\t5180\t-60\t[ESS]\t\n"
        "ZZ:11:22:33:44:ee\t5180\t-10\t[ESS]\tInvalid\n"
        "00:11:22:33:44:ff\t5180\tgarbage\t[ESS]\tInvalid\n";
    auto networks = ParseScan(scan);
    assert(networks.size() == 7);
    assert(networks[0].ssid == "Cafe WiFi" && networks[0].signal == -40);
    assert(networks[0].security == Security::Psk && networks[1].security == Security::Open);
    assert(networks[2].security == Security::Sae);
    for (const auto& n : networks)
        if (n.ssid == "Enterprise" || n.ssid == "Old" || n.ssid == "Unknown") assert(n.security == Security::Unsupported);
    assert(!ValidPassword(Security::Psk, "short"));
    assert(!ValidPassword(Security::Psk, "hello\nworld"));
    assert(!ValidPassword(Security::Psk, std::string(64, 'z')));
    assert(ValidPassword(Security::Psk, std::string(64, 'a')));
    assert(ValidPassword(Security::Sae, "x"));
    assert(ValidPassword(Security::Psk, "пароль123"));
    assert(!ValidPassword(Security::Unsupported, "12345678"));
    assert(!ValidPassword(Security::Open, "ignored"));
    Network hostile{"x\"\nREMOVE_NETWORK all", "00:11:22:33:44:55", Security::Psk, -40};
    auto commands = Configure(hostile, "a\"\\bcdefg", 3);
    assert(commands.size() == 4);
    assert(commands[0] == "SET_NETWORK 3 ssid " + Hex(hostile.ssid));
    assert(commands[2] == "SET_NETWORK 3 psk \"a\\\"\\\\bcdefg\"");
    for (const auto& c : commands) assert(c.find('\n') == std::string::npos);
    assert(Configure(hostile, "bad", 3).empty());
    assert(Configure(hostile, "12345678", -1).empty());
    assert(Field("ip_address=1.2.3.4\nwpa_state=COMPLETED\n", "wpa_state") == "COMPLETED");
    assert(Field("other_wpa_state=COMPLETED\n", "wpa_state").empty());
    std::string many = scan;
    for (int i = 0; i < 100; ++i)
        many += "00:11:22:33:44:55\t2412\t-60\t[ESS]\tnetwork" + std::to_string(i) + "\n";
    assert(ParseScan(many).size() == 32);

    std::cout << "Wi-Fi parsing, security and configuration tests passed.\n";
    if (getenv("DODGE_TEST_NO_SOCKETS")) {
        std::cout << "Socket integration test explicitly skipped (restricted runtime).\n";
        return 0;
    }
    // Real AF_UNIX exchange with a fake supplicant, including timeout isolation.
    std::string path = "wifi-test-" + std::to_string(getpid()) + ".sock";
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0); assert(fd >= 0);
    sockaddr_un local{}; local.sun_family = AF_UNIX;
    strcpy(local.sun_path, path.c_str());
    assert(bind(fd, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == 0);
    std::thread server([&] {
        char data[256]; sockaddr_un peer{}; socklen_t len = sizeof(peer);
        ssize_t bytes = recvfrom(fd, data, sizeof(data), 0, reinterpret_cast<sockaddr*>(&peer), &len);
        assert(std::string(data, bytes) == "STATUS");
        std::string reply = "wpa_state=DISCONNECTED\n";
        assert(sendto(fd, reply.data(), reply.size(), 0, reinterpret_cast<sockaddr*>(&peer), len) > 0);
        len = sizeof(peer);
        bytes = recvfrom(fd, data, sizeof(data), 0, reinterpret_cast<sockaddr*>(&peer), &len);
        assert(std::string(data, bytes) == "SCAN");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        sendto(fd, "LATE\n", 5, 0, reinterpret_cast<sockaddr*>(&peer), len);
        len = sizeof(peer);
        bytes = recvfrom(fd, data, sizeof(data), 0, reinterpret_cast<sockaddr*>(&peer), &len);
        assert(std::string(data, bytes) == "PING");
        assert(sendto(fd, "PONG\n", 5, 0, reinterpret_cast<sockaddr*>(&peer), len) > 0);
    });
    assert(Request(path, "STATUS") == "wpa_state=DISCONNECTED\n");
    assert(Request(path, "SCAN", 20).empty());
    assert(Request(path, "PING") == "PONG\n");
    assert(Request(path, "SET_NETWORK 1 psk x\nREMOVE_NETWORK all").empty());
    server.join(); close(fd); unlink(path.c_str());
    assert(Request("missing-control-socket", "PING", 10).empty());
    std::cout << "Wi-Fi parsing, security, configuration and control-socket tests passed.\n";
}
