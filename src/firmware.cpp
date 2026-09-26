#include <CommonCrypto/CommonDigest.h>
#include <dirent.h>
#include <sys/stat.h>

#include <cstdio>

#include "firmware.h"

const std::vector<KnownFirmware> &known_firmware() {
    static const std::vector<KnownFirmware> table = {
        { "os1.62", "r162.da1", "OS 1.62", "OZ-750", "a66c0b0e602464d44e1fb5083fb0e2b6e8d28ae920016875abfc51222c9b8311", 589824,
          "r162.da1 from the Sharp System Update Utility v1.62" },
        { "os2.1", "os2.1-firmware.bin", "OS 2.1", "ZQ-770", "e56c8391f94f579505d41c3d05d0b103801cb340648c4c79e9898b44ec19812a", 589824,
          "pages 000-047 of a ZQ-770's flash, dumped with OZDump" },
    };
    return table;
}

std::string file_sha256(const std::string &path) {
    FILE *file = fopen(path.c_str(), "rb");
    if (!file) return "";
    CC_SHA256_CTX context;
    CC_SHA256_Init(&context);
    unsigned char buffer[65536];
    size_t count;
    while ((count = fread(buffer, 1, sizeof buffer, file)) > 0) CC_SHA256_Update(&context, buffer, (CC_LONG)count);
    fclose(file);
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(digest, &context);
    static const char hex[] = "0123456789abcdef";
    std::string text;
    for (unsigned char value : digest) {
        text += hex[value >> 4];
        text += hex[value & 15];
    }
    return text;
}

static long file_size(const std::string &path) {
    struct stat info;
    if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) return -1;
    return (long)info.st_size;
}

const KnownFirmware *identify_firmware(const std::string &path) {
    long size = file_size(path);
    std::string digest;
    for (const KnownFirmware &known : known_firmware()) {
        if (size != (long)known.size) continue;
        if (digest.empty()) digest = file_sha256(path);
        if (digest == known.sha256) return &known;
    }
    return nullptr;
}

std::vector<FirmwareFile> find_firmware(const std::string &directory) {
    std::vector<FirmwareFile> found;
    DIR *listing = opendir(directory.c_str());
    if (!listing) return found;
    std::vector<std::string> names;
    while (struct dirent *entry = readdir(listing)) {
        if (entry->d_name[0] != '.') names.push_back(entry->d_name);
    }
    closedir(listing);
    for (const KnownFirmware &known : known_firmware()) {
        for (const std::string &name : names) {
            std::string path = directory + "/" + name;
            if (identify_firmware(path) == &known) {
                found.push_back({ &known, path });
                break;
            }
        }
    }
    return found;
}

std::string state_file_name(const KnownFirmware *known, const std::string &path) {
    if (known) return std::string("state-") + known->id + ".bin";
    return "state-" + path.substr(path.rfind('/') + 1) + ".bin";
}
