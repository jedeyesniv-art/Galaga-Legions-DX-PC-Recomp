// Steady menus (--galaga_steady_ui, on by default): removes the 15 Hz strobe from the menu skin.
//
// The menus' shared skin (geui\XB360\Skin.geui, a XUI-style layout) has looping timelines whose opacity keyframes
// alternate 1.0 / 0.5 every 2 frames for the whole 64-frame loop - an additive glow over the mode select, mission
// preview and result screens that flickers at 15 Hz, uncomfortable to look at (the same in Xenia). As the file is
// read, every long run of such alternating keyframes gets one steady opacity, the run's average (0.75), so the
// screens keep their brightness without the strobe. Fade-ins and the short confirm blinks (a few keyframes) are
// left alone. Nothing on disk changes.
//
// Keyframe records, as far as they matter here: 00 01 | frame (u16) | 00 00 | property count (1-2) | 04 | opacity
// (big-endian float) | other properties. A timeline's records follow each other at a fixed stride.

#include <rex/cvar.h>
#include <rex/filesystem/entry.h>
#include <rex/filesystem/file.h>
#include <rex/logging.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

REXCVAR_DEFINE_BOOL(galaga_steady_ui, true, "Galaga",
                    "Remove the rapid opacity strobe from the menu skin (mode select, previews, results)");

namespace {

struct Keyframe {
  size_t opacity_offset;  // of the big-endian float
  uint32_t frame;
  float opacity;
};

float LoadFloat(const uint8_t* p) {
  return std::bit_cast<float>(uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]);
}

void StoreFloat(uint8_t* p, float value) {
  const uint32_t bits = std::bit_cast<uint32_t>(value);
  p[0] = uint8_t(bits >> 24);
  p[1] = uint8_t(bits >> 16);
  p[2] = uint8_t(bits >> 8);
  p[3] = uint8_t(bits);
}

std::vector<Keyframe> FindKeyframes(std::span<const uint8_t> data) {
  std::vector<Keyframe> keyframes;
  for (size_t i = 0; i + 12 <= data.size(); i++) {
    const uint8_t* p = data.data() + i;
    if (p[0] == 0 && p[1] == 1 && p[4] == 0 && p[5] == 0 && (p[6] == 1 || p[6] == 2) && p[7] == 4)
      keyframes.push_back({i + 8, uint32_t(p[2]) << 8 | p[3], LoadFloat(p + 8)});
  }
  return keyframes;
}

// Flattens runs of at least MinAlternations keyframes, each at most 2 frames after the previous one, whose opacity
// keeps jumping by more than 0.2. Returns the number of keyframes changed.
size_t SteadyStrobes(std::span<uint8_t> data, int& runs) {
  constexpr size_t MinAlternations = 8;
  const std::vector<Keyframe> keyframes = FindKeyframes(data);
  size_t changed = 0;
  size_t i = 0;
  while (i < keyframes.size()) {
    // Grow a run of fast, alternating keyframes of one timeline (same record stride).
    size_t j = i + 1;
    const size_t stride = j < keyframes.size() ? keyframes[j].opacity_offset - keyframes[i].opacity_offset : 0;
    while (j < keyframes.size() && stride > 0 && stride <= 64 &&
           keyframes[j].opacity_offset - keyframes[j - 1].opacity_offset == stride &&
           keyframes[j].frame > keyframes[j - 1].frame && keyframes[j].frame - keyframes[j - 1].frame <= 2 &&
           std::fabs(keyframes[j].opacity - keyframes[j - 1].opacity) > 0.2f)
      j++;
    if (j - i > MinAlternations) {
      float sum = 0;
      for (size_t k = i; k < j; k++)
        sum += keyframes[k].opacity;
      const float steady = sum / float(j - i);
      for (size_t k = i; k < j; k++)
        StoreFloat(data.data() + keyframes[k].opacity_offset, steady);
      changed += j - i;
      runs++;
      i = j;
    } else {
      i++;
    }
  }
  return changed;
}

void Filter(const rex::filesystem::Entry& entry, size_t byte_offset, std::span<uint8_t> data) {
  if (!REXCVAR_GET(galaga_steady_ui))
    return;
  std::string name = entry.name();
  std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  if (name != "skin.geui")
    return;
  if (byte_offset != 0 || data.size() != entry.size()) {
    REXLOG_WARN("steady UI: {} read in parts ({} bytes at {}), left unchanged", entry.path(), data.size(), byte_offset);
    return;
  }
  int runs = 0;
  const size_t changed = SteadyStrobes(data, runs);
  REXLOG_INFO("steady UI: {}: {} strobing timelines made steady ({} keyframes)", entry.path(), runs, changed);
}

}  // namespace

void SteadyUiInstall() { rex::filesystem::SetFileReadFilter(Filter); }
