#pragma once
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <cstdio>
#include <cerrno>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#endif
#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#endif
namespace PaperBoatSave {
// Write beside the destination so replacement is atomic on the same filesystem.
// A failed write/flush/rename leaves the previous destination untouched.
inline void AtomicWrite(const std::filesystem::path& destination, const std::string& bytes) {
    const auto temporary = destination.string() + ".tmp";
    FILE* file = std::fopen(temporary.c_str(), "wb");
    if (!file) throw std::runtime_error("Unable to open temporary save");
    bool ok = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    if (std::fflush(file) != 0) ok = false;
#if defined(__unix__) || defined(__APPLE__)
    if (fsync(fileno(file)) != 0) ok = false;
#endif
    if (std::fclose(file) != 0) ok = false;
    if (!ok) throw std::runtime_error("Unable to flush temporary save");
#ifdef _WIN32
    if (!MoveFileExW(std::filesystem::path(temporary).c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Unable to replace save");
#else
    std::filesystem::rename(temporary, destination);
#endif
#if defined(__unix__) || defined(__APPLE__)
    int directory = open(destination.parent_path().c_str(), O_RDONLY);
    if (directory >= 0) { fsync(directory); close(directory); }
#endif
}
inline std::string Read(const std::filesystem::path& path) {
    if (std::filesystem::file_size(path) > 4 * 1024 * 1024)
        throw std::runtime_error("Save exceeds size limit");
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Unable to open save");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
}
