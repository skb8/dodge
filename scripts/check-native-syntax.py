#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Host syntax check of the native adapters against the recovery API surface.

This is supplemental: the Android recovery build remains the integration check.
"""
from pathlib import Path
import shutil
import subprocess
root = Path(__file__).resolve().parents[1]
work = root / '.tests-build/adapters'
(work / 'dodge_wifi').mkdir(parents=True, exist_ok=True)
(work / 'gui').mkdir(exist_ok=True)
(work / 'cutils').mkdir(exist_ok=True)
(work / 'modprobe').mkdir(exist_ok=True)
for file in (root / 'wifi').glob('*'):
    if file.suffix in ['.cpp', '.hpp']:
        shutil.copy2(file, work / 'dodge_wifi' / file.name)
(work / 'data.hpp').write_text('''#pragma once
#include <string>
#include <functional>
// Match the std namespace exposure of the real recovery data.hpp.
using namespace std;
class DataManager { public:
static std::string GetStrValue(const std::string&);
static int SetValue(const std::string&, const std::string&, int = 0);
};
''')
(work / 'gui/pages.hpp').write_text('''#pragma once
#include <string>
class PageManager { public: static int ChangePage(std::string); static std::string GetCurrentPage(); };
''')
(work / 'gui/gui.hpp').write_text('''#pragma once
#include <string>
std::string gui_parse_text(std::string);
''')
(work / 'cutils/properties.h').write_text('''#pragma once
#define PROPERTY_VALUE_MAX 92
extern "C" int property_get(const char*, char*, const char*);
extern "C" int property_set(const char*, const char*);
''')
(work / 'partitions.hpp').write_text('''#pragma once
#include <string>
class TWPartition { public: bool Is_Mounted(); bool Mount(bool); bool UnMount(bool); };
class TWPartitionManager { public: TWPartition* Find_Partition_By_Path(std::string); bool Mount_By_Path(std::string,bool); };
extern TWPartitionManager PartitionManager;
''')
(work / 'twrp-functions.hpp').write_text('''#pragma once
#include <string>
namespace TWFunc { bool Recursive_Mkdir(std::string); int Exec_Cmd(const std::string&, bool = true); }
''')
(work / 'modprobe/modprobe.h').write_text('''#pragma once
#include <string>
#include <vector>
class Modprobe { public: Modprobe(const std::vector<std::string>&, const std::string&, bool); bool LoadListedModules(bool); };
''')
subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread', '-fsyntax-only',
                '-I' + str(work), str(work / 'dodge_wifi/wifi.cpp'),
                str(work / 'dodge_wifi/wifi_prepare.cpp')], check=True)
print('Native adapter C++ syntax checks passed (Android build still required).')
