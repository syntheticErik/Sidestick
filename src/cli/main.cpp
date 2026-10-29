// SPDX-FileCopyrightText: 2026 Erik Evans
// SPDX-License-Identifier: GPL-3.0-or-later
// sidestick-cli: command-line front end for the LM Drum core library.
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "audio.h"
#include "device.h"
#include "model.h"
#include "sysexfile.h"

using namespace lmd;

namespace {

const char* kUsage =
    "usage: sidestick-cli [--port NAME] [--any-firmware] <command> [args]\n"
    "\n"
    "  --port NAME      MIDI port to use (default: first port containing \"LM DRUM\")\n"
    "  --any-firmware   allow writes on firmware versions that have not been tested\n"
    "\n"
    "device commands:\n"
    "  ports                         list MIDI ports\n"
    "  info                          firmware version, pool size, user memory used/free\n"
    "  pool [--all]                  list user samples (--all includes the 109 factory samples)\n"
    "  bank <1-16>                   show a bank's name, kit, and used slots\n"
    "  banks                         one line per bank\n"
    "  backup <file.ars>             full backup (user samples + banks), SynthTribe-compatible\n"
    "  upload <file.wav> [--name N]  convert and upload to the pool only (not assigned to any bank)\n"
    "  slot <bank> <slot> <pool#|empty> [--force]\n"
    "                                put a pool sample in a bank slot, or remove it (empty);\n"
    "                                removing refuses if a kit voice plays that slot, unless --force\n"
    "  voice <bank> <voice> <slot>   point a kit voice (1-16 or name, e.g. high-tom) at a bank slot\n"
    "  rename <bank> <name>          rename a bank (stored upper-case, 16 characters)\n"
    "  clear-kit <bank> --yes        silence every voice and reset voice settings (slots kept)\n"
    "  clear-bank <bank> --yes       reset a bank to the untouched state (samples stay in the pool)\n"
    "  restore <file.ars> [--yes]    replace ALL user samples and banks with a backup (without --yes:\n"
    "                                check the file and show what would change); verified afterwards\n"
    "  export <bank> <file.syx>      save a bank as a SynthTribe-compatible .syx\n"
    "  import <file.syx> <bank>      write a .syx bank export into a bank (checks its pool references)\n"
    "\n"
    "offline commands:\n"
    "  extract <file.ars> <dir>      write a backup's user samples as WAVs and list its banks\n"
    "  convert <in.wav> <out.wav>    convert a file to the device format (24 kHz, 16-bit, mono)\n";

std::string slotLabel(uint16_t v) {
    if (v == kSlotOff) return "OFF";
    if (v == kSlotEmpty) return "---";
    return std::to_string(v);
}

void printBank(int index, const Bank& b) {
    std::printf("bank %d \"%s\"\n  kit:\n", index + 1, b.name().c_str());
    for (int v = 0; v < kVoices; ++v) std::printf("    %-11s slot %3d\n", kVoiceNames[v], b.voiceSlot(v));
    std::printf("  slots -> pool index:\n");
    for (int s = 1; s < kSlots; ++s)
        if (b.slot(s) != kSlotEmpty) std::printf("    %3d: %s\n", s, slotLabel(b.slot(s)).c_str());
}

int usedSlots(const Bank& b) {
    int n = 0;
    for (int s = 1; s < kSlots; ++s) n += b.slot(s) != kSlotEmpty;
    return n;
}

int bankArg(const std::string& s) {
    int n = std::stoi(s);
    if (n < 1 || n > kBanks) throw std::runtime_error("bank must be 1-16");
    return n - 1;
}

int slotArg(const std::string& s) {
    int n = std::stoi(s);
    if (n < 1 || n >= kSlots) throw std::runtime_error("slot must be 1-127 (slot 0 is the fixed OFF slot)");
    return n;
}

int voiceArg(const std::string& s) {
    if (!s.empty() && std::isdigit(static_cast<unsigned char>(s[0]))) {
        int n = std::stoi(s);
        if (n < 1 || n > kVoices) throw std::runtime_error("voice number must be 1-16");
        return n - 1;
    }
    std::string want;
    for (char c : s) want += (c == '-' || c == '_') ? ' ' : char(std::toupper(static_cast<unsigned char>(c)));
    for (int v = 0; v < kVoices; ++v)
        if (want == kVoiceNames[v]) return v;
    throw std::runtime_error("unknown voice \"" + s + "\"");
}

// Read-modify-write of one bank, verified by reading it back.
void editBank(Device& d, int b, const std::function<void(Bank&)>& edit) {
    Bank bank = d.readBank(b);
    edit(bank);
    d.writeBank(b, bank);
    if (!(d.readBank(b) == bank)) throw std::runtime_error("bank " + std::to_string(b + 1) + " did not read back as written");
}

int run(std::vector<std::string> args) {
    std::string port = "LM DRUM";
    bool anyFirmware = false;
    while (!args.empty() && args[0].rfind("--", 0) == 0) {
        if (args[0] == "--port" && args.size() >= 2) {
            port = args[1];
            args.erase(args.begin(), args.begin() + 2);
        } else if (args[0] == "--any-firmware") {
            anyFirmware = true;
            args.erase(args.begin());
        } else {
            break;
        }
    }
    // Writes are only allowed on firmware the protocol has been verified against.
    auto guard = [&](Device& d) {
        std::string v = d.firmwareVersion();
        for (const char* t : kTestedFirmware)
            if (v == t) return;
        if (!anyFirmware)
            throw std::runtime_error("firmware " + v + " has not been tested for writes; use --any-firmware to override");
        std::fprintf(stderr, "warning: firmware %s has not been tested for writes\n", v.c_str());
    };
    if (args.empty()) {
        std::fputs(kUsage, stderr);
        return 2;
    }
    const std::string cmd = args[0];
    auto need = [&](size_t n) {
        if (args.size() < n + 1) throw std::runtime_error("missing arguments for " + cmd + "\n\n" + kUsage);
    };

    if (cmd == "ports") {
        for (auto& p : Device::inputPorts()) std::printf("in:  %s\n", p.c_str());
        for (auto& p : Device::outputPorts()) std::printf("out: %s\n", p.c_str());
    } else if (cmd == "info") {
        Device d(port);
        auto pool = d.pool();
        uint64_t used = 0;
        for (size_t i = kFactorySamples; i < pool.size(); ++i) used += sampleStorageBytes(pool[i].frames);
        std::printf("firmware %s\npool: %zu samples (%d factory, %zu user)\n", d.firmwareVersion().c_str(), pool.size(),
                    kFactorySamples, pool.size() - kFactorySamples);
        std::printf("user memory: %.2f of %.2f MiB used (%.1f%%), about %.1f s of %.0f s free\n", used / 1048576.0,
                    kUserMemoryBytes / 1048576.0, 100.0 * used / kUserMemoryBytes,
                    (kUserMemoryBytes - std::min(used, kUserMemoryBytes)) / 2.0 / kSampleRate,
                    kUserMemoryBytes / 2.0 / kSampleRate);
    } else if (cmd == "pool") {
        Device d(port);
        bool all = args.size() > 1 && args[1] == "--all";
        auto pool = d.pool();
        for (size_t i = all ? 0 : kFactorySamples; i < pool.size(); ++i)
            std::printf("%3zu  %-16s %7u frames  %7.3f s\n", i, pool[i].name.c_str(), pool[i].frames,
                        pool[i].frames / double(kSampleRate));
    } else if (cmd == "bank") {
        need(1);
        int b = bankArg(args[1]);
        printBank(b, Device(port).readBank(b));
    } else if (cmd == "banks") {
        for (auto& [i, b] : Device(port).readAllBanks())
            std::printf("%2d  %-16s %3d slots used\n", i + 1, b.name().c_str(), usedSlots(b));
    } else if (cmd == "backup") {
        need(1);
        auto frames = Device(port).backupFrames();
        writeSysexFile(args[1], frames);
        Backup bk = parseBackup(frames);
        std::printf("wrote %s: %zu frames, %zu user samples, %zu banks\n", args[1].c_str(), frames.size(),
                    bk.samples.size(), bk.banks.size());
    } else if (cmd == "upload") {
        need(1);
        std::string name = std::filesystem::path(args[1]).stem().string();
        if (args.size() >= 4 && args[2] == "--name") name = args[3];
        name = deviceName(name);
        Bytes pcm = convertToDevicePcm(args[1]);
        Device d(port);
        guard(d);
        auto r = d.uploadUnassigned(name, pcm);
        std::printf("uploaded \"%s\" (%zu frames, %.3f s) as pool index %d\n", name.c_str(), pcm.size() / 2,
                    pcm.size() / 2 / double(kSampleRate), r.poolIndex);
        if (r.devicePlacement)
            std::printf("device had filed it in bank %d slot %d; removed, so it is in the pool only\n",
                        r.devicePlacement->bank + 1, r.devicePlacement->slot);
    } else if (cmd == "slot") {
        need(3);
        int b = bankArg(args[1]), s = slotArg(args[2]);
        bool force = args.size() > 4 && args[4] == "--force";
        Device d(port);
        guard(d);
        uint16_t value = kSlotEmpty;
        if (args[3] != "empty") {
            int idx = std::stoi(args[3]);
            auto pool = d.pool();
            if (idx < 0 || idx >= int(pool.size()))
                throw std::runtime_error("pool index must be 0-" + std::to_string(pool.size() - 1));
            value = uint16_t(idx);
        }
        editBank(d, b, [&](Bank& bank) {
            if (value == kSlotEmpty && !force)
                for (int v = 0; v < kVoices; ++v)
                    if (bank.voiceSlot(v) == s)
                        throw std::runtime_error(std::string(kVoiceNames[v]) + " plays slot " + std::to_string(s) +
                                                 "; removing it would silence that voice (use --force)");
            if (value == kSlotEmpty) bank.setSlot(s, value);
            else bank.putSample(s, value);
        });
        std::printf("bank %d slot %d = %s\n", b + 1, s, slotLabel(value).c_str());
    } else if (cmd == "voice") {
        need(3);
        int b = bankArg(args[1]), v = voiceArg(args[2]);
        int s = std::stoi(args[3]);
        if (s < 0 || s >= kSlots) throw std::runtime_error("slot must be 0-127");
        Device d(port);
        guard(d);
        editBank(d, b, [&](Bank& bank) { bank.setVoiceSlot(v, uint16_t(s)); });
        std::printf("bank %d %s -> slot %d\n", b + 1, kVoiceNames[v], s);
    } else if (cmd == "rename") {
        need(2);
        int b = bankArg(args[1]);
        Device d(port);
        guard(d);
        std::string name;
        editBank(d, b, [&](Bank& bank) {
            bank.setName(args[2]);
            name = bank.name();
        });
        std::printf("bank %d renamed \"%s\"\n", b + 1, name.c_str());
    } else if (cmd == "restore") {
        need(1);
        auto frames = readSysexFile(args[1]);
        auto problems = restoreProblems(frames);
        for (auto& p : problems) std::fprintf(stderr, "  problem: %s\n", p.c_str());
        if (!problems.empty()) throw std::runtime_error("not restoring " + args[1]);
        Backup bk = parseBackup(frames);
        size_t sampleFrames = frames.size() - bk.banks.size();
        Device d(port);
        guard(d);
        auto pool = d.pool();
        std::printf("device now: %zu user samples\nfile:       %zu user samples, %zu banks (%zu frames)\n",
                    pool.size() - kFactorySamples, bk.samples.size(), bk.banks.size(), frames.size());
        if (!(args.size() > 2 && args[2] == "--yes")) {
            std::printf("this replaces ALL user samples and banks on the device; run again with --yes to proceed\n");
            return 1;
        }
        auto t0 = std::chrono::steady_clock::now();
        d.restore(frames, [&](size_t n, size_t total) {
            if (n % 100 == 0 || n == total) std::printf("  %zu/%zu frames\n", n, total);
            if (n + 1 == sampleFrames) std::printf("  last sample frame: the device commits to flash before replying (can take minutes)\n");
            std::fflush(stdout);
        });
        double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("restored in %.1f s; verifying with a fresh backup...\n", secs);
        if (d.backupFrames() != frames) throw std::runtime_error("verification FAILED: the device's backup differs from the file");
        std::printf("verified: the device's backup is byte-identical to %s\n", args[1].c_str());
    } else if (cmd == "clear-kit" || cmd == "clear-bank") {
        need(1);
        int b = bankArg(args[1]);
        if (!(args.size() > 2 && args[2] == "--yes")) {
            std::printf("this %s bank %d%s; add --yes to proceed\n",
                        cmd == "clear-kit" ? "resets the kit of" : "empties", b + 1,
                        b < 7 ? " (a factory bank: only a backup can bring it back)" : "");
            return 1;
        }
        Device d(port);
        guard(d);
        editBank(d, b, [&](Bank& bank) { cmd == "clear-kit" ? bank.clearKit() : bank.clear(); });
        std::printf("bank %d: %s\n", b + 1, cmd == "clear-kit" ? "kit cleared" : "cleared");
    } else if (cmd == "export") {
        need(2);
        int b = bankArg(args[1]);
        Bank bank = Device(port).readBank(b);
        writeSysexFile(args[2], {dataFrame(uint8_t(kTypeBank + b), bank.bytes())});
        std::printf("bank %d \"%s\" -> %s\n", b + 1, bank.name().c_str(), args[2].c_str());
    } else if (cmd == "import") {
        need(2);
        int b = bankArg(args[2]);
        std::string err;
        auto bank = bankFromExport(readSysexFile(args[1]), &err);
        if (!bank) throw std::runtime_error(args[1] + ": " + err);
        Device d(port);
        guard(d);
        auto missing = missingSampleSlots(*bank, d.pool().size());
        if (!missing.empty())
            throw std::runtime_error("slot " + std::to_string(missing[0]) + " refers to pool index " +
                                     std::to_string(bank->slot(missing[0])) + ", which this device doesn't have");
        d.writeBank(b, *bank);
        if (!(d.readBank(b) == *bank)) throw std::runtime_error("bank did not read back as written");
        std::printf("%s -> bank %d \"%s\"\n", args[1].c_str(), b + 1, bank->name().c_str());
    } else if (cmd == "extract") {
        need(2);
        Backup bk = parseBackup(readSysexFile(args[1]));
        std::filesystem::create_directories(args[2]);
        for (size_t i = 0; i < bk.samples.size(); ++i) {
            char fn[64];
            std::snprintf(fn, sizeof fn, "%03zu %s.wav", i, bk.samples[i].name.c_str());
            std::string safe = fn;
            for (char& c : safe)
                if (c == '/') c = '_';
            writeDeviceWav((std::filesystem::path(args[2]) / safe).string(), bk.samples[i].pcm);
            std::printf("%3zu  %-16s %7zu frames\n", i, bk.samples[i].name.c_str(), bk.samples[i].pcm.size() / 2);
        }
        for (auto& [i, b] : bk.banks) std::printf("bank %2d  %-16s %3d slots used\n", i + 1, b.name().c_str(), usedSlots(b));
    } else if (cmd == "convert") {
        need(2);
        Bytes pcm = convertToDevicePcm(args[1]);
        writeDeviceWav(args[2], pcm);
        std::printf("wrote %s: %zu frames, %.3f s\n", args[2].c_str(), pcm.size() / 2, pcm.size() / 2 / double(kSampleRate));
    } else {
        std::fprintf(stderr, "unknown command: %s\n\n%s", cmd.c_str(), kUsage);
        return 2;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(std::vector<std::string>(argv + 1, argv + argc));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
