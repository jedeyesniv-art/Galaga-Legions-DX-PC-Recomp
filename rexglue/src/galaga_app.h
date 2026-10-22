// galaga - ReXGlue Recompiled Project
//
// Run defaults carried over from the Pac-Man CE port (see CLAUDE.md for why each one exists).

#pragma once

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/logging/api.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/system/gpu_plugin.h>
#include <rex/system/interfaces/input.h>
#include <rex/ui/flags.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/window.h>
#include <rex/ui/overlay/achievement_notification.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include <windows.h> // EnumDisplaySettingsW

// XamContentGetLicenseMask returns this; 0 (the SDK default) makes the game run as the locked XBLA trial.
REXCVAR_DECLARE(uint32_t, license_mask);
// Lets the presenter present from the GPU (guest output) thread after each guest frame instead of painting on
// the UI thread at the display refresh rate.
REXCVAR_DECLARE(bool, host_present_from_non_ui_thread);
// Keep the game's 16:9 image unstretched in any window shape (pillar/letterbox bars instead of stretching).
REXCVAR_DECLARE(bool, present_letterbox);
// Lets the sampling profiler (profiler.cpp, --galaga_profile) find the UI thread.
void ProfilerNoteUiThread();
// Removes the menu skin's opacity strobe as it loads (steady_ui.cpp, --galaga_steady_ui).
void SteadyUiInstall();
// Startup checks for players (game_files.cpp).
std::string CheckGameData(const std::filesystem::path& root, const std::filesystem::path& game_dir);
void ShowGameDataError(const std::string& message);
void BackupSaves(const std::filesystem::path& user_data);
// Graphics API of the GPU plugin: vulkan (default), d3d12 or any (see OnPreSetup).
REXCVAR_DECLARE(std::string, galaga_gpu_backend);

class GalagaApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    auto app = std::unique_ptr<GalagaApp>(new GalagaApp(ctx, "galaga", PPCImageConfig));
    instance_ = app.get();
    return app;
  }

  static GalagaApp* instance() { return instance_; }

  // The game's Achievements menu item calls XamShowAchievementsUI, a stub in the SDK, so it would do nothing. It
  // opens the SDK's achievements overlay (the F7 bind, bind_achievements) instead. B on the pad closes it again (as
  // on the console; main.cpp's input hook, which also keeps the game from seeing the pad meanwhile), so does F7.
  // Called from guest threads. The bind toggles, so the open state is tracked (also in OnKeyDown).
  void SetAchievementsOverlay(bool open) {
    app_context().CallInUIThread([this, open] {
      if (window() == nullptr || achievements_open_.load() == open)
        return;
      rex::ui::KeyEvent press(window(), AchievementsKey(), 1, false, false, false, false, false);
      if (rex::ui::ProcessKeyEvent(press))
        achievements_open_.store(open);
    });
  }
  bool achievements_open() const { return achievements_open_.load(); }

  // Defaults so galaga.exe runs with no arguments; --game_data_root and --gpu_plugin still override them.

  void OnConfigurePaths(rex::PathConfig& paths) override {
    const auto exe_dir = rex::filesystem::GetExecutableFolder();
    if (paths.game_data_root.empty())
      paths.game_data_root = FindGameData(exe_dir);
    UsePortableUserData(exe_dir, paths);
    game_data_root_found_ = paths.game_data_root;
    game_dir_ = exe_dir / "game";
    user_data_dir_ = paths.user_data_root;
  }

  // After the base class has set up paths and logging, before anything else starts: tell the player clearly when
  // the game files are missing or belong to another game (instead of a cryptic failure later), and back up the
  // save data.
  bool SetupEnvironment() override {
    if (!ReXApp::SetupEnvironment())
      return false;
    if (const std::string problem = CheckGameData(game_data_root_found_, game_dir_); !problem.empty()) {
      REXLOG_ERROR("game data: {}", problem);
      ShowGameDataError(problem);
      return false;
    }
    BackupSaves(user_data_dir_);
    return true;
  }

  std::filesystem::path game_data_root_found_, game_dir_, user_data_dir_;

  // Game data, first match: game\ next to the exe (the portable layout) holding the unpacked files or the game's
  // package file (STFS, as dumped from the console; mounted by SDK patch 0009), the exe's own folder, or the
  // nearest private\ searching up from the exe (the repo layout, with the build in rexglue\out\build\<preset>\).
  // Empty if none is found, so the SDK asks for it.
  static std::filesystem::path FindGameData(const std::filesystem::path& exe_dir) {
    if (std::filesystem::exists(exe_dir / "game" / "default.xex"))
      return exe_dir / "game";
    if (auto package = FindGamePackage(exe_dir / "game"); !package.empty())
      return package;
    if (std::filesystem::exists(exe_dir / "default.xex"))
      return exe_dir;
    for (auto dir = exe_dir; dir.has_relative_path(); dir = dir.parent_path()) {
      if (std::filesystem::exists(dir / "private" / "default.xex"))
        return dir / "private";
    }
    return {};
  }

  // Galaga Legions DX's package file anywhere under dir (dumps often keep the console's 58410AF9\000D0000\
  // folders): an STFS header (LIVE, CON or PIRS) with the game's title ID at 0x360.
  static std::filesystem::path FindGamePackage(const std::filesystem::path& dir) {
    constexpr uint8_t TitleId[] = {0x58, 0x41, 0x0A, 0xF9};
    std::error_code error;
    for (auto it = std::filesystem::recursive_directory_iterator(dir, error);
         !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
      if (it.depth() > 3) {
        it.disable_recursion_pending();
        continue;
      }
      if (!it->is_regular_file(error) || it->file_size(error) < 0x1000)
        continue;
      std::ifstream file(it->path(), std::ios::binary);
      uint8_t header[0x364] = {};
      if (!file.read(reinterpret_cast<char*>(header), sizeof(header)))
        continue;
      const bool stfs = std::memcmp(header, "LIVE", 4) == 0 || std::memcmp(header, "CON ", 4) == 0 ||
                        std::memcmp(header, "PIRS", 4) == 0;
      if (stfs && std::memcmp(header + 0x360, TitleId, 4) == 0)
        return it->path();
    }
    return {};
  }

  // Saves and the shader cache go in userdata\ next to the exe, so the folder is self-contained. Falls back to
  // the SDK default (Documents\galaga) when that folder can't be created, e.g. under Program Files. Paths set
  // with --user_data_root / --cache_root are left alone.
  static void UsePortableUserData(const std::filesystem::path& exe_dir, rex::PathConfig& paths) {
    const auto sdk_user_dir = rex::filesystem::GetUserFolder() / "galaga";
    if (paths.user_data_root != sdk_user_dir)
      return;  // set on the command line

    const auto portable_dir = exe_dir / "userdata";
    std::error_code error;
    std::filesystem::create_directories(portable_dir, error);
    if (error)
      return;

    if (paths.cache_root == sdk_user_dir / "cache")
      paths.cache_root = portable_dir / "cache";
    paths.user_data_root = portable_dir;
  }

  // GPU: the SDK leaves GPU emulation off unless a plugin is named. Vulkan by default (one rendering path on
  // Windows and under Proton, where it skips vkd3d-proton); --galaga_gpu_backend=d3d12 switches back.
  // The plugin is loaded here rather than by ReXApp right after this hook, so its settings (the render
  // resolution) exist and can be given defaults before the GPU is set up.
  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (config.gpu_plugin.empty())
      config.gpu_plugin = "xenos";
    if (!config.graphics) {
      const std::string backend = REXCVAR_GET(galaga_gpu_backend);
      config.graphics = rex::system::LoadGpuPlugin(config.gpu_plugin, backend);
      if (!config.graphics && backend != "any") {
        REXLOG_WARN("GPU backend '{}' unavailable in this build, using the default", backend);
        config.graphics = rex::system::LoadGpuPlugin(config.gpu_plugin);
      }
      if (config.graphics) {
        ApplyDesktopResolutionScale();
        ApplyGpuWaitStep();
        ApplyStreamingBlocks();
      }
    }
  }

  // shared_memory_streaming_blocks (our SDK patch 0001): memory the game keeps rewriting (per-draw vertex
  // rings) is re-uploaded on use instead of write-protected, saving an access fault per rewrite. A big win under
  // Wine/Proton and measurable on Windows in Pac-Man; re-check for Galaga once it renders.
  // Settings given on the command line or in the config file win.
  static void ApplyStreamingBlocks() {
    if (rex::cvar::GetFlagSource("shared_memory_streaming_blocks") == rex::cvar::Source::kDefault &&
        !rex::cvar::SetFlagByName("shared_memory_streaming_blocks", "true"))
      REXLOG_INFO("this GPU plugin has no shared_memory_streaming_blocks");
    REXLOG_INFO("{}: shared_memory_streaming_blocks={}", RunningUnderWine() ? "Wine/Proton" : "Windows",
                rex::cvar::GetFlagByName("shared_memory_streaming_blocks"));
  }

  // WAIT_REG_MEM: the SDK sleeps 1 ms per failed check (up to 15.6 ms at Windows' default timer resolution);
  // gpu_wait_reg_mem_sleep_us (our SDK patch 0002) polls in short high-resolution steps instead.
  static void ApplyGpuWaitStep() {
    if (rex::cvar::GetFlagSource("gpu_wait_reg_mem_sleep_us") == rex::cvar::Source::kDefault)
      rex::cvar::SetFlagByName("gpu_wait_reg_mem_sleep_us", "200");
  }

  // Wine (and so Proton) exports wine_get_version from its ntdll; Windows never does.
  static bool RunningUnderWine() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll != nullptr && GetProcAddress(ntdll, "wine_get_version") != nullptr;
  }

  void OnPostInitLogging() override {
    ProfilerNoteUiThread();
    SteadyUiInstall();
    REXLOG_INFO("Galaga Legions DX " GALAGA_VERSION " build {} {}; profiler {}", __DATE__, __TIME__,
                rex::cvar::GetFlagByName("galaga_profile") == "true" ? "on" : "off");

    // Present like Xenia does: from the guest output thread, once per guest frame. When the presenter paints on
    // the UI thread instead (at the display refresh), the game's swap waits about one host refresh per frame for
    // that painting's GPU work, and heavy frames miss their 60 Hz vblank (Pac-Man's "lag storms").
    REXCVAR_SET(host_present_from_non_ui_thread, true);

    if (!rex::cvar::HasNonDefaultValue("present_letterbox"))
      REXCVAR_SET(present_letterbox, true);
    // Start fullscreen unless --fullscreen=false (or the config file) says otherwise.
    if (rex::cvar::GetFlagSource("fullscreen") == rex::cvar::Source::kDefault)
      REXCVAR_SET(fullscreen, true);

    ApplyKeyboardControls();

    // License: bit 0 marks the game as purchased, unlocking the full version instead of the trial. A non-zero
    // --license_mask still wins (an explicit 0 can't be told apart from "not set").
    if (REXCVAR_GET(license_mask) == 0)
      REXCVAR_SET(license_mask, 1);
    REXLOG_INFO("license_mask = 0x{:X} ({})", REXCVAR_GET(license_mask),
                (REXCVAR_GET(license_mask) & 1) ? "full version" : "trial");
  }

  // Keyboard controls through the SDK's keyboard-as-controller driver (off by default). It merges into player 1
  // together with any real pad. Bind values are comma lists of key names; modifiers must match exactly. The SDK's
  // own defaults spread the pad over half the keyboard, so every bind is set here. Binds given on the command line
  // or in the config file win.
  // TODO: Galaga's real control set (fire, satellite placement) once the game runs.
  static void ApplyKeyboardControls() {
    struct Bind {
      const char* name;
      const char* keys;
    };
    static constexpr Bind Binds[] = {
        {"keybind_dpad_up", "Up,W"},     {"keybind_dpad_down", "Down,S"},
        {"keybind_dpad_left", "Left,A"}, {"keybind_dpad_right", "Right,D"},
        {"keybind_a", "Space,Return"},   {"keybind_b", "Escape"},
        {"keybind_start", "P"},          {"keybind_back", ""},
        {"keybind_x", ""},               {"keybind_y", ""},
        {"keybind_left_trigger", ""},    {"keybind_right_trigger", ""},
        {"keybind_left_shoulder", ""},   {"keybind_right_shoulder", ""},
        {"keybind_lstick_up", ""},       {"keybind_lstick_down", ""},
        {"keybind_lstick_left", ""},     {"keybind_lstick_right", ""},
        {"keybind_lstick_press", ""},    {"keybind_rstick_up", ""},
        {"keybind_rstick_down", ""},     {"keybind_rstick_left", ""},
        {"keybind_rstick_right", ""},    {"keybind_rstick_press", ""},
        {"keybind_guide", ""},
    };
    if (rex::cvar::GetFlagSource("mnk_mode") == rex::cvar::Source::kDefault)
      rex::cvar::SetFlagByName("mnk_mode", "true");
    for (const Bind& bind : Binds) {
      if (rex::cvar::GetFlagSource(bind.name) == rex::cvar::Source::kDefault &&
          !rex::cvar::SetFlagByName(bind.name, bind.keys))
        REXLOG_WARN("keyboard controls: couldn't set {}", bind.name);
    }
  }

  // No achievement pop-ups. The toast is the one ImGui dialog that always exists, and any dialog registers ImGui
  // as a UI drawer, which forces the presenter to paint on the UI thread at the display refresh rate (see
  // OnPostInitLogging). Achievements are still recorded; the F7 overlay still lists them.
  std::unique_ptr<rex::ui::AchievementNotificationDialog> CreateAchievementNotificationDialog() override {
    return nullptr;
  }

  // Render resolution from the desktop: the largest 16:9 area that fits the primary display, divided by the
  // game's 1280x720 and rounded up to a whole scale. --resolution_scale / --draw_resolution_scale_x / _y (or the
  // config file) still override it. These settings belong to the GPU plugin, so they are set by name.
  static void ApplyDesktopResolutionScale() {
    constexpr int32_t GuestWidth = 1280, GuestHeight = 720, MaxScale = 7;
    for (const char* name : {"resolution_scale", "draw_resolution_scale_x", "draw_resolution_scale_y"}) {
      if (rex::cvar::GetFlagSource(name) != rex::cvar::Source::kDefault)
        return;
    }

    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) || mode.dmPelsWidth == 0 ||
        mode.dmPelsHeight == 0)
      return;

    const double fit = std::min(double(mode.dmPelsWidth) / GuestWidth, double(mode.dmPelsHeight) / GuestHeight);
    const int32_t scale = std::clamp(int32_t(std::ceil(fit - 0.01)), 1, MaxScale);
    const auto value = std::to_string(scale);
    if (!rex::cvar::SetFlagByName("draw_resolution_scale_x", value) ||
        !rex::cvar::SetFlagByName("draw_resolution_scale_y", value)) {
      REXLOG_WARN("render resolution: couldn't set the scale (GPU plugin settings missing)");
      return;
    }
    REXLOG_INFO("render resolution: {}x (desktop {}x{}, renders {}x{})", scale, mode.dmPelsWidth, mode.dmPelsHeight,
                GuestWidth * scale, GuestHeight * scale);
  }

  // F11 or Alt+Enter switches between fullscreen and windowed (the SDK has no key for it). Other keys go to
  // the SDK's key bindings (F3/F4/F7 overlays). Switching mode makes SDL send the still-held key again, so
  // presses soon after a switch are ignored.
  void OnKeyDown(rex::ui::KeyEvent& e) override {
    using rex::ui::VirtualKey;
    const bool toggle = e.virtual_key() == VirtualKey::kF11 ||
                        (e.virtual_key() == VirtualKey::kReturn && e.is_alt_pressed());
    if (toggle && window() != nullptr) {
      constexpr auto MinInterval = std::chrono::milliseconds(500);
      const auto now = std::chrono::steady_clock::now();
      // prev_state = auto-repeat (the SDL backend always reports repeat_count 1)
      if (!e.prev_state() && now - last_fullscreen_toggle_ >= MinInterval) {
        last_fullscreen_toggle_ = now;
        window()->SetFullscreen(!window()->IsFullscreen());
        REXLOG_INFO("fullscreen {}", window()->IsFullscreen() ? "on" : "off");
      }
      e.set_handled(true);
      return;
    }
    // Track the achievements overlay's state: the bind toggles it (see OpenAchievementsOverlay).
    const bool achievements_key = e.virtual_key() == AchievementsKey();
    if (rex::ui::ProcessKeyEvent(e) && achievements_key)
      achievements_open_.store(!achievements_open_.load());
  }

  static rex::ui::VirtualKey AchievementsKey() {
    return rex::ui::ParseVirtualKey(rex::cvar::GetFlagByName("bind_achievements"));
  }

  static inline GalagaApp* instance_ = nullptr;
  std::atomic<bool> achievements_open_{false};  // written on the UI thread, read by the input hook

  // Shut input down while the window still exists. ReXApp::OnDestroy destroys the window before the runtime,
  // and the runtime's shutdown then destroys the mouse/keyboard input driver, whose destructor detaches from the
  // already-freed window: heap corruption (0xC0000374) on every exit that doesn't go through the window's
  // OnClosing (e.g. quitting from the game's menu). InputSystem::Shutdown is safe to call again later.
  void OnShutdown() override {
    if (rex::Runtime* rt = runtime(); rt != nullptr && rt->input_system() != nullptr)
      rt->input_system()->Shutdown();
  }

  // The game doesn't use the mouse, so hide the cursor over the window (it shows while the mouse moves).
  // Called on the UI thread once the window exists.
  void OnCreateDialogs(rex::ui::ImGuiDrawer*) override {
    if (window() == nullptr)
      return;
    window()->SetTitle("Galaga Legions DX (PC port " GALAGA_VERSION ")");  // instead of the SDK stamp
    window()->SetCursorAutoHideDelayMs(1500);
    window()->SetCursorVisibility(rex::ui::Window::CursorVisibility::kAutoHidden);
  }

  // How the game ended, for unexplained exits: "Execution complete" alone doesn't tell a normal quit from the
  // game stopping itself.
  bool OnWindowCloseRequested() override {
    close_requested_ = true;
    REXLOG_INFO("window close requested");
    return true;
  }
  void OnGuestThreadExit(rex::system::XThread*) override {
    REXLOG_WARN("game main thread ended ({})",
                close_requested_ ? "after a window close request" : "no window close request: the game stopped itself");
    rex::FlushLogging();
  }

  bool close_requested_ = false;
  std::chrono::steady_clock::time_point last_fullscreen_toggle_{};
};
