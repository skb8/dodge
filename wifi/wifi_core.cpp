// SPDX-License-Identifier: GPL-3.0-or-later
#include "wifi_core.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <poll.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace dodge_wifi {
namespace {
int Digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
std::string Quote(const std::string& text) {
    std::string out = "\"";
    for (char c : text) { if (c == '\\' || c == '"') out += '\\'; out += c; }
    return out + '"';
}
struct Socket {
    int fd;
    explicit Socket(int value) : fd(value) {}
    ~Socket() { if (fd >= 0) close(fd); }
};
bool Ready(int fd, short events, int ms) {
    pollfd p{fd, events, 0};
    int r;
    do { r = poll(&p, 1, ms); } while (r < 0 && errno == EINTR);
    return r > 0 && (p.revents & events);
}
bool BindInterface(int fd, const std::string& iface) {
    timeval timeout{3, 0};
    return setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, iface.c_str(), iface.size() + 1) == 0 &&
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0 &&
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0;
}
// Only skip DNS names; never follow compression pointers into untrusted packets.
bool SkipName(const unsigned char* p, size_t len, size_t& pos) {
    while (pos < len) {
        unsigned n = p[pos++];
        if (n == 0) return true;
        if ((n & 0xc0) == 0xc0) { if (pos >= len) return false; ++pos; return true; }
        if (n > 63 || pos + n > len) return false;
        pos += n;
    }
    return false;
}
}
std::string Hex(const std::string& text) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    for (unsigned char c : text) { out += digits[c >> 4]; out += digits[c & 15]; }
    return out;
}
std::string DecodeSSID(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            char c = text[++i];
            if (c == 'x' && i + 2 < text.size() && Digit(text[i+1]) >= 0 && Digit(text[i+2]) >= 0) {
                out += static_cast<char>(Digit(text[i+1]) * 16 + Digit(text[i+2])); i += 2;
            } else if (c == 'n') out += '\n';
            else if (c == 'r') out += '\r';
            else if (c == 't') out += '\t';
            else if (c == 'e') out += '\033';
            else out += c;
        } else out += text[i];
    }
    return out;
}
std::string Label(const std::string& text) {
    std::string out;
    // Prevent network names from being interpreted as OrangeFox variable/resource markup.
    for (unsigned char c : text) out += (c < 32 || c == 127 || c == '%' || c == '{' || c == '}') ? '?' : c;
    return out;
}
std::string SecurityName(Security s) {
    switch (s) {
        case Security::Open: return "Open";
        case Security::Psk: return "WPA/WPA2";
        case Security::Sae: return "WPA3";
        case Security::Owe: return "Enhanced Open";
        default: return "Unsupported security";
    }
}
std::vector<Network> ParseScan(const std::string& reply) {
    std::map<std::pair<std::string, Security>, Network> unique;
    std::istringstream lines(reply);
    std::string line;
    while (std::getline(lines, line)) {
        std::vector<std::string> columns;
        size_t start = 0;
        for (int i = 0; i < 4; ++i) {
            size_t end = line.find('\t', start);
            if (end == std::string::npos) break;
            columns.push_back(line.substr(start, end - start)); start = end + 1;
        }
        if (columns.size() != 4 || columns[0].size() != 17) continue;
        bool mac = true;
        for (size_t i = 0; i < 17; ++i)
            if ((i % 3 == 2 && columns[0][i] != ':') || (i % 3 != 2 && Digit(columns[0][i]) < 0)) mac = false;
        if (!mac) continue;
        Network n;
        n.bssid = columns[0]; n.ssid = DecodeSSID(line.substr(start));
        if (n.ssid.empty() || n.ssid.size() > 32) continue;
        char* end = nullptr;
        long signal = strtol(columns[2].c_str(), &end, 10);
        if (!end || *end || signal < -127 || signal > 0) continue;
        n.signal = static_cast<int>(signal);
        auto& flags = columns[3];
        // EAP, WEP, IBSS and unknown protected AKMs must never fall back to open.
        if (flags.find("EAP") != std::string::npos || flags.find("WEP") != std::string::npos ||
            flags.find("IBSS") != std::string::npos) n.security = Security::Unsupported;
        else if (flags.find("SAE") != std::string::npos) n.security = Security::Sae;
        else if (flags.find("PSK") != std::string::npos) n.security = Security::Psk;
        else if (flags.find("OWE") != std::string::npos) n.security = Security::Owe;
        else if (flags.find("WPA") != std::string::npos || flags.find("RSN") != std::string::npos ||
                 flags.find("PRIVACY") != std::string::npos) n.security = Security::Unsupported;
        else {
            std::string open_flags = flags;
            for (const char* flag : {"[ESS]", "[WPS]", "[WPS-PBC]", "[WPS-PIN]"}) {
                size_t at;
                while ((at = open_flags.find(flag)) != std::string::npos) open_flags.erase(at, strlen(flag));
            }
            n.security = open_flags.empty() ? Security::Open : Security::Unsupported;
        }
        auto key = std::make_pair(n.ssid, n.security);
        auto old = unique.find(key);
        if (old == unique.end() || old->second.signal < n.signal) unique[key] = n;
    }
    std::vector<Network> out;
    for (const auto& item : unique) out.push_back(item.second);
    std::sort(out.begin(), out.end(), [](const Network& a, const Network& b) {
        if (a.signal != b.signal) return a.signal > b.signal;
        if (a.ssid != b.ssid) return a.ssid < b.ssid;
        return a.security < b.security;
    });
    if (out.size() > 32) out.resize(32);
    return out;
}
bool ValidPassword(Security s, const std::string& p) {
    if (s == Security::Unsupported) return false;
    if (s == Security::Open || s == Security::Owe) return p.empty();
    if (s == Security::Psk && p.size() == 64)
        return std::all_of(p.begin(), p.end(), [](char c) { return Digit(c) >= 0; });
    if (p.size() < (s == Security::Sae ? 1u : 8u) || p.size() > 63) return false;
    return std::all_of(p.begin(), p.end(), [](unsigned char c) { return c >= 32 && c != 127; });
}
std::vector<std::string> Configure(const Network& n, const std::string& p, int id) {
    if (id < 0 || n.ssid.empty() || n.ssid.size() > 32 || !ValidPassword(n.security, p)) return {};
    std::string prefix = "SET_NETWORK " + std::to_string(id) + " ";
    std::vector<std::string> out{prefix + "ssid " + Hex(n.ssid)};
    switch (n.security) {
        case Security::Psk:
            out.push_back(prefix + "key_mgmt WPA-PSK");
            out.push_back(prefix + "psk " + (p.size() == 64 ? p : Quote(p))); break;
        case Security::Sae:
            out.push_back(prefix + "key_mgmt SAE");
            out.push_back(prefix + "ieee80211w 2");
            out.push_back(prefix + "sae_password " + Quote(p)); break;
        case Security::Owe:
            out.push_back(prefix + "key_mgmt OWE");
            out.push_back(prefix + "ieee80211w 2"); break;
        case Security::Open: out.push_back(prefix + "key_mgmt NONE"); break;
        default: return {};
    }
    out.push_back("SELECT_NETWORK " + std::to_string(id));
    return out;
}
std::string Field(const std::string& status, const std::string& key) {
    std::istringstream lines(status);
    std::string line;
    while (std::getline(lines, line)) if (line.compare(0, key.size()+1, key + "=") == 0) return line.substr(key.size()+1);
    return {};
}
std::string Request(const std::string& path, const std::string& command, int timeout_ms) {
    if (command.empty() || command.size() > 4096 || command.find('\n') != std::string::npos ||
        command.find('\r') != std::string::npos || command.find('\0') != std::string::npos) return {};
    Socket sock(socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0));
    if (sock.fd < 0) return {};
    static std::atomic<unsigned> seq{0};
    sockaddr_un local{}, remote{};
    local.sun_family = remote.sun_family = AF_UNIX;
    // Linux abstract socket, so interrupted clients leave no filesystem entries.
    snprintf(local.sun_path + 1, sizeof(local.sun_path) - 1, "dodge-wifi-%d-%u", getpid(), ++seq);
    if (path.size() >= sizeof(remote.sun_path)) return {};
    memcpy(remote.sun_path, path.c_str(), path.size() + 1);
    if (bind(sock.fd, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0 ||
        connect(sock.fd, reinterpret_cast<sockaddr*>(&remote), sizeof(remote)) < 0 ||
        send(sock.fd, command.data(), command.size(), 0) != static_cast<ssize_t>(command.size()) ||
        !Ready(sock.fd, POLLIN, timeout_ms)) return {};
    char buffer[32768];
    ssize_t n = recv(sock.fd, buffer, sizeof(buffer), 0);
    if (n <= 0 || n == sizeof(buffer)) return {};
    return std::string(buffer, static_cast<size_t>(n));
}
int CheckInternet(const std::string& dns, const std::string& iface) {
    // Bionic DNS needs netd, absent in recovery. Ask the DHCP-provided DNS server directly.
    Socket udp(socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0));
    sockaddr_in server{}; server.sin_family = AF_INET; server.sin_port = htons(53);
    if (udp.fd < 0 || inet_pton(AF_INET, dns.c_str(), &server.sin_addr) != 1 || !BindInterface(udp.fd, iface)) return 0;
    if (connect(udp.fd, reinterpret_cast<sockaddr*>(&server), sizeof(server)) < 0) return 0;
    unsigned char packet[512]{};
    unsigned short transaction = 0;
    FILE* random = fopen("/dev/urandom", "rb");
    if (!random) return 0;
    const size_t random_size = fread(&transaction, 1, sizeof(transaction), random); fclose(random);
    if (random_size != sizeof(transaction)) return 0;
    packet[0] = transaction >> 8; packet[1] = transaction & 255; packet[2] = 1; packet[5] = 1;
    size_t pos = 12;
    for (const std::string& label : {std::string("connectivitycheck"), std::string("gstatic"), std::string("com")}) {
        packet[pos++] = label.size(); memcpy(packet + pos, label.data(), label.size()); pos += label.size();
    }
    packet[pos++] = 0; packet[pos++] = 0; packet[pos++] = 1; packet[pos++] = 0; packet[pos++] = 1;
    if (send(udp.fd, packet, pos, 0) != static_cast<ssize_t>(pos)) return 0;
    ssize_t bytes = recv(udp.fd, packet, sizeof(packet), 0);
    if (bytes < 12 || packet[0] != (transaction >> 8) || packet[1] != (transaction & 255) ||
        !(packet[2] & 0x80) || (packet[2] & 0x02) || (packet[3] & 15)) return 0;
    size_t len = bytes; pos = 12;
    unsigned questions = packet[4]*256 + packet[5], answers = packet[6]*256 + packet[7];
    for (unsigned i = 0; i < questions; ++i) {
        if (!SkipName(packet, len, pos) || pos + 4 > len) return 0;
        pos += 4;
    }
    in_addr address{};
    bool found = false;
    for (unsigned i = 0; i < answers; ++i) {
        if (!SkipName(packet, len, pos) || pos + 10 > len) return 0;
        unsigned type = packet[pos]*256 + packet[pos+1], cls = packet[pos+2]*256 + packet[pos+3];
        unsigned size = packet[pos+8]*256 + packet[pos+9]; pos += 10;
        if (pos + size > len) return 0;
        if (type == 1 && cls == 1 && size == 4) { memcpy(&address, packet+pos, 4); found = true; break; }
        pos += size;
    }
    if (!found) return 0;
    Socket tcp(socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0));
    if (tcp.fd < 0 || !BindInterface(tcp.fd, iface)) return 0;
    server.sin_addr = address; server.sin_port = htons(80);
    int result = connect(tcp.fd, reinterpret_cast<sockaddr*>(&server), sizeof(server));
    if (result < 0 && errno != EINPROGRESS) return 0;
    if (!Ready(tcp.fd, POLLOUT, 3000)) return 0;
    int error = 0; socklen_t error_len = sizeof(error);
    if (getsockopt(tcp.fd, SOL_SOCKET, SO_ERROR, &error, &error_len) < 0 || error) return 0;
    const std::string request = "GET /generate_204 HTTP/1.1\r\nHost: connectivitycheck.gstatic.com\r\nConnection: close\r\n\r\n";
    if (send(tcp.fd, request.data(), request.size(), MSG_NOSIGNAL) != static_cast<ssize_t>(request.size())) return 0;
    std::string header;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (header.size() < 1024 && std::chrono::steady_clock::now() < deadline) {
        if (!Ready(tcp.fd, POLLIN, 300)) continue;
        char data[256]; ssize_t n = recv(tcp.fd, data, sizeof(data), 0);
        if (n <= 0) break;
        header.append(data, n);
        if (header.find("\r\n") != std::string::npos) break;
    }
    int code = 0;
    if (header.compare(0, 7, "HTTP/1.") != 0 || sscanf(header.c_str(), "HTTP/%*d.%*d %d", &code) != 1) return 0;
    return code;
}
}
