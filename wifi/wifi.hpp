// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
namespace dodge_wifi {
// GUI-thread API. Pump publishes worker snapshots; no worker touches the GUI.
void Pump();
void Action(const std::string& action);
bool PrepareDriver();
}
