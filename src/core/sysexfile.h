// SPDX-FileCopyrightText: 2026 Erik Evans
// SPDX-License-Identifier: GPL-3.0-or-later
// Raw SysEx files: .syx (bank export) and .ars (SynthTribe backup) are frames stored end to end.
#pragma once

#include <string>
#include <vector>

#include "protocol.h"

namespace lmd {

std::vector<Bytes> readSysexFile(const std::string& path);
void writeSysexFile(const std::string& path, const std::vector<Bytes>& frames);

}  // namespace lmd
