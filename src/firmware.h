#pragma once
#include <cstddef>
#include <string>
#include <vector>

struct KnownFirmware {
    const char *id;
    const char *file;
    const char *title;
    const char *model;
    const char *sha256;
    size_t      size;
    const char *source;
};

struct FirmwareFile {
    const KnownFirmware *known;
    std::string path;
};

const std::vector<KnownFirmware> &known_firmware();
std::string file_sha256(const std::string &path);
const KnownFirmware *identify_firmware(const std::string &path);
std::vector<FirmwareFile> find_firmware(const std::string &directory);
std::string state_file_name(const KnownFirmware *known, const std::string &path);
