// galaga - ReXGlue Recompiled Project

#include "generated/default/galaga_init.h"

#include "galaga_app.h"

#include <rex/system/xmemory.h>

REXCVAR_DEFINE_STRING(galaga_gpu_backend, "vulkan", "Galaga",
                      "Graphics API: vulkan (falls back to the plugin's default if it has no Vulkan), d3d12, or any "
                      "(the plugin's default: D3D12 on Windows)")
    .allowed({"any", "d3d12", "vulkan"});

// The game's Achievements menu item: entry of sub_8215A858, its wrapper around XamShowAchievementsUI (a stub in
// the SDK). Opens the SDK's achievements overlay instead.
void AchievementsUiHook() {
  if (GalagaApp* app = GalagaApp::instance())
    app->SetAchievementsOverlay(true);
}

// The game's controller poll (sub_820B7AC0 loops over the 4 users: r27 = user; XInputGetState wrapper result in r3,
// XINPUT_STATE at r1+0xA0: packet number, then the gamepad: buttons u16, triggers, thumbsticks - 12 bytes).
// While the achievements overlay is open the game sees no buttons, as with the console's system UI, and a new B
// press closes the overlay. Afterwards the pads stay blanked until every button is released, so the B that closed
// it doesn't reach the game too. The keyboard's Escape is B (merged into user 0 by the SDK).
void AchievementsInputHook(PPCRegister& r1, PPCRegister& r3, PPCRegister& r27) {
  constexpr uint16_t ButtonB = 0x2000;
  static uint16_t previous[4] = {};
  static bool held_after_close[4] = {};
  GalagaApp* app = GalagaApp::instance();
  const uint32_t user = r27.u32;
  if (app == nullptr || r3.u32 != 0 || user >= 4)
    return;
  uint8_t* gamepad = rex::Runtime::instance()->memory()->TranslateVirtual<uint8_t*>(r1.u32 + 0xA0 + 4);
  const uint16_t buttons = uint16_t(gamepad[0] << 8 | gamepad[1]);
  const bool open = app->achievements_open();
  if (open) {
    if ((buttons & ButtonB) && !(previous[user] & ButtonB))
      app->SetAchievementsOverlay(false);
    held_after_close[user] = true;
  } else if (held_after_close[user] && buttons == 0) {
    held_after_close[user] = false;
  }
  previous[user] = buttons;
  if (open || held_after_close[user])
    std::memset(gamepad, 0, 12);
}

REX_DEFINE_APP(galaga, GalagaApp::Create)
