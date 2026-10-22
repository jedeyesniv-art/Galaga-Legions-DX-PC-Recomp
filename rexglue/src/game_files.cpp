// Player-facing checks at startup: are the game files there and are they Galaga Legions DX (a clear message instead of a
// cryptic failure), and automatic backups of the save data.

#include <rex/logging.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <string>
#include <vector>

#include <windows.h>  // MessageBoxW

namespace {

constexpr uint32_t GalagaTitleId = 0x58410AF9;

uint32_t BigEndian32(const uint8_t* bytes) {
  return uint32_t(bytes[0]) << 24 | uint32_t(bytes[1]) << 16 | uint32_t(bytes[2]) << 8 | bytes[3];
}

std::vector<uint8_t> ReadStart(const std::filesystem::path& path, size_t size) {
  std::vector<uint8_t> bytes(size);
  std::ifstream file(path, std::ios::binary);
  file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(size));
  bytes.resize(size_t(file.gcount()));
  return bytes;
}

bool IsPackage(const std::vector<uint8_t>& header) {
  return header.size() >= 0x364 && (std::memcmp(header.data(), "LIVE", 4) == 0 ||
                                    std::memcmp(header.data(), "CON ", 4) == 0 ||
                                    std::memcmp(header.data(), "PIRS", 4) == 0);
}

// A package's display name (UTF-16BE at 0x411), ASCII only - just for the message.
std::string PackageName(const std::vector<uint8_t>& header) {
  std::string name;
  for (size_t i = 0x411; i + 1 < header.size() && i < 0x411 + 0x80; i += 2) {
    if (header[i] == 0 && header[i + 1] == 0)
      break;
    name += header[i] == 0 && header[i + 1] < 0x80 ? char(header[i + 1]) : '?';
  }
  return name;
}

// Title ID in an XEX2's execution info (optional header 0x00040006: media ID, version, base version, title ID).
uint32_t XexTitleId(const std::filesystem::path& xex) {
  const std::vector<uint8_t> header = ReadStart(xex, 0x10000);
  if (header.size() < 0x18 || std::memcmp(header.data(), "XEX2", 4) != 0)
    return 0;
  const uint32_t count = BigEndian32(&header[0x14]);
  for (uint32_t i = 0; i < count && 0x18 + 8 * i + 8 <= header.size(); ++i) {
    const uint32_t key = BigEndian32(&header[0x18 + 8 * i]), value = BigEndian32(&header[0x1C + 8 * i]);
    if (key == 0x00040006 && value + 16 <= header.size())
      return BigEndian32(&header[value + 12]);
  }
  return 0;
}

std::string DifferentGame(uint32_t title_id, const std::string& name) {
  return fmt::format(
      "These game files are for a different game{}{} (title ID {:08X}), not Galaga Legions DX "
      "(title ID 58410AF9).",
      name.empty() ? "" : ", ", name, title_id);
}

const char* const HowToSetUp =
    "Put your own copy of the game in the game folder next to galaga.exe, either:\n"
    "  - the game's package file as dumped from your console (a file with a long hexadecimal name), or\n"
    "  - the unpacked files: default.xex with the data folder.";

}  // namespace

// Empty if the game data is usable, else a message for the player. root: what the app will mount (a folder, a
// package file or empty if nothing was found); game_dir: the game\ folder next to the exe.
std::string CheckGameData(const std::filesystem::path& root, const std::filesystem::path& game_dir) {
  std::error_code error;
  if (root.empty()) {
    // Nothing usable found - but maybe a package of another game is sitting in game\.
    for (auto it = std::filesystem::recursive_directory_iterator(game_dir, error);
         !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
      if (it.depth() > 3 || !it->is_regular_file(error))
        continue;
      const std::vector<uint8_t> header = ReadStart(it->path(), 0x500);
      if (IsPackage(header))
        return DifferentGame(BigEndian32(&header[0x360]), PackageName(header)) + "\n\nFound: " +
               it->path().string() + "\n\n" + HowToSetUp;
    }
    return fmt::format("Galaga Legions DX's game files were not found.\n\n{}\n\nGame folder: {}",
                       HowToSetUp, game_dir.string());
  }
  if (std::filesystem::is_regular_file(root, error)) {
    const std::vector<uint8_t> header = ReadStart(root, 0x500);
    if (!IsPackage(header))
      return fmt::format("{}\nis not an Xbox 360 game package.\n\n{}", root.string(), HowToSetUp);
    if (const uint32_t title_id = BigEndian32(&header[0x360]); title_id != GalagaTitleId)
      return DifferentGame(title_id, PackageName(header)) + "\n\n" + root.string();
    return {};
  }
  const auto xex = root / "default.xex";
  if (!std::filesystem::exists(xex, error))
    return fmt::format("There is no default.xex in\n{}\n\n{}", root.string(), HowToSetUp);
  if (const uint32_t title_id = XexTitleId(xex); title_id != GalagaTitleId)
    return DifferentGame(title_id, "") + "\n\n" + root.string();
  return {};
}

// The message box for CheckGameData's messages, titled with the game's name (the SDK's says "ReXGlue").
void ShowGameDataError(const std::string& message) {
  const int length = MultiByteToWideChar(CP_UTF8, 0, message.data(), int(message.size()), nullptr, 0);
  std::wstring text(size_t(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, message.data(), int(message.size()), text.data(), length);
  MessageBoxW(nullptr, text.c_str(), L"Galaga Legions DX", MB_OK | MB_ICONERROR);
}

// Copies the save data (everything in user_data except cache\ and save-backups\) to
// user_data\save-backups\<date_time>\ when it differs from the newest backup; keeps the newest 10.
void BackupSaves(const std::filesystem::path& user_data) {
  namespace fs = std::filesystem;
  constexpr size_t Keep = 10;
  std::error_code error;
  const fs::path backups = user_data / "save-backups";

  std::vector<fs::path> files;  // relative to user_data
  for (auto it = fs::recursive_directory_iterator(user_data, error);
       !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
    const fs::path relative = it->path().lexically_relative(user_data);
    const std::string top = relative.begin()->string();
    if (top == "cache" || top == "save-backups" || top == "logs") {
      it.disable_recursion_pending();
      continue;
    }
    if (it->is_regular_file(error))
      files.push_back(relative);
  }
  if (files.empty())
    return;

  std::vector<fs::path> existing;
  for (const auto& entry : fs::directory_iterator(backups, error))
    if (entry.is_directory())
      existing.push_back(entry.path());
  std::sort(existing.begin(), existing.end());  // names are date_time, so this is oldest first

  auto read = [](const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
  };
  if (!existing.empty()) {
    bool same = true;
    size_t backed_up = 0;
    for (auto it = fs::recursive_directory_iterator(existing.back(), error);
         !error && it != fs::recursive_directory_iterator(); it.increment(error))
      backed_up += it->is_regular_file(error) ? 1 : 0;
    same = backed_up == files.size();
    for (size_t i = 0; same && i < files.size(); ++i)
      same = read(user_data / files[i]) == read(existing.back() / files[i]);
    if (same)
      return;
  }

  const std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_s(&local, &now);
  char name[32];
  std::strftime(name, sizeof(name), "%Y-%m-%d_%H%M%S", &local);
  const fs::path target = backups / name;
  for (const fs::path& file : files) {
    fs::create_directories((target / file).parent_path(), error);
    fs::copy_file(user_data / file, target / file, fs::copy_options::overwrite_existing, error);
    if (error) {
      REXLOG_WARN("save backup: could not copy {}: {}", file.string(), error.message());
      return;
    }
  }
  existing.push_back(target);
  while (existing.size() > Keep) {
    fs::remove_all(existing.front(), error);
    existing.erase(existing.begin());
  }
  REXLOG_INFO("save backup: {} ({} files)", target.string(), files.size());
}
