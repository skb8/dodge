// SPDX-License-Identifier: GPL-3.0-or-later
#include "wifi.hpp"
#include "../partitions.hpp"
#include "../twrp-functions.hpp"
#include <modprobe/modprobe.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>
#include <dirent.h>

namespace dodge_wifi {
bool PrepareDriver() {
    // Keep compatible modules from the installed slot ahead of the optional source build.
    // Never force-load a module: the kernel enforces vermagic, signatures and symbol CRCs.
    if (access("/sys/class/net/wlan0", F_OK) == 0) return true;
    TWFunc::Recursive_Mkdir("/tmp/dodge-wifi");
    TWFunc::Exec_Cmd("/system/bin/sh /system/bin/dodge-wifi-stage source");
    TWPartition* vendor = PartitionManager.Find_Partition_By_Path("/vendor");
    TWPartition* dlkm = PartitionManager.Find_Partition_By_Path("/vendor_dlkm");
    bool vendor_was_mounted = vendor && vendor->Is_Mounted();
    bool dlkm_was_mounted = dlkm && dlkm->Is_Mounted();
    if (vendor) vendor->Mount(false);
    if (dlkm) dlkm->Mount(false);
    int staged = TWFunc::Exec_Cmd("/system/bin/sh /system/bin/dodge-wifi-stage stock");
    if (vendor && !vendor_was_mounted) vendor->UnMount(false);
    if (dlkm && !dlkm_was_mounted) dlkm->UnMount(false);
    if (staged != 0) return false;
    // Firmware stays on the read-only modem filesystem; it is not copied into the image.
    if (!PartitionManager.Mount_By_Path("/firmware", false)) return false;
    if (TWFunc::Exec_Cmd("/system/bin/sh /system/bin/dodge-wifi-stage firmware") != 0) return false;
    for (const std::string dir : {"/tmp/dodge-wifi/stock", "/tmp/dodge-wifi/source"}) {
        DIR* stream = opendir(dir.c_str());
        if (!stream) continue;
        std::ofstream list(dir + "/modules.load.dodge_wifi");
        bool found = false;
        while (dirent* entry = readdir(stream)) {
            std::string name(entry->d_name);
            if (name.size() > 3 && name.substr(name.size() - 3) == ".ko" &&
                (name.find("qca_cld3_") == 0 || name == "wlan.ko")) {
                list << name << '\n'; found = true;
            }
        }
        closedir(stream); list.close();
        if (found) {
            Modprobe loader({dir}, "modules.load.dodge_wifi", false);
            loader.LoadListedModules(false);
            // Probe/firmware load is asynchronous.
            for (int i = 0; i < 50; ++i) {
                if (access("/sys/class/net/wlan0", F_OK) == 0) return true;
                usleep(100000);
            }
        }
    }
    return false;
}
}
