#ifndef DESIGN_TOKENS_H
#define DESIGN_TOKENS_H

#include "../thirdparty/imgui/imgui.h"

namespace DesignTokens {

// ====================================================
// COLOR SYSTEM
// ====================================================
const ImVec4 BgMain           = ImVec4(0.059f, 0.090f, 0.165f, 1.00f); // #0F172A
const ImVec4 BgSecondary      = ImVec4(0.086f, 0.125f, 0.200f, 1.00f); // #162033
const ImVec4 CardBg           = ImVec4(0.118f, 0.161f, 0.231f, 1.00f); // #1E293B
const ImVec4 CardElevated     = ImVec4(0.141f, 0.196f, 0.278f, 1.00f); // #243247
const ImVec4 AccentPrimary    = ImVec4(0.133f, 0.827f, 0.933f, 1.00f); // #22D3EE (Cyan)
const ImVec4 AccentSecondary  = ImVec4(0.063f, 0.725f, 0.506f, 1.00f); // #10B981 (Emerald)
const ImVec4 ColorDanger      = ImVec4(0.937f, 0.267f, 0.267f, 1.00f); // #EF4444
const ImVec4 ColorWarning     = ImVec4(0.961f, 0.620f, 0.043f, 1.00f); // #F59E0B
const ImVec4 TextPrimary      = ImVec4(0.973f, 0.980f, 0.988f, 1.00f); // #F8FAFC
const ImVec4 TextSecondary    = ImVec4(0.580f, 0.639f, 0.722f, 1.00f); // #94A3B8
const ImVec4 BorderColor      = ImVec4(1.000f, 1.000f, 1.000f, 0.05f); // rgba(255,255,255,0.05)
const ImVec4 HoverGlow        = ImVec4(1.000f, 1.000f, 1.000f, 0.08f); // rgba(255,255,255,0.08)

// ====================================================
// TYPOGRAPHY SIZES
// ====================================================
constexpr float FontTitleSize   = 26.0f;
constexpr float FontSectionSize = 18.0f;
constexpr float FontBodySize    = 14.0f;
constexpr float FontCaptionSize = 12.0f;

// ====================================================
// GRID SPACING TOKENS (8px Grid System)
// ====================================================
constexpr float GridUnit       = 8.0f;
constexpr float OuterMargin    = 20.0f;
constexpr float CardPadding    = 20.0f;
constexpr float SectionSpacing = 24.0f;
constexpr float ControlSpacing = 12.0f;

// ====================================================
// CORNER RADII
// ====================================================
constexpr float CardRadius     = 12.0f;
constexpr float ButtonRadius   = 10.0f;
constexpr float FrameRadius    = 8.0f;
constexpr float PillRadius     = 11.0f;

// ====================================================
// CONTROL HEIGHTS
// ====================================================
constexpr float ServerBtnHeight = 42.0f;
constexpr float StatusBarHeight = 36.0f;
constexpr float ToggleSwitchH   = 20.0f;
constexpr float ToggleSwitchW   = 38.0f;

// ====================================================
// ANIMATION TIMINGS (MS)
// ====================================================
constexpr float HoverTimeMs     = 100.0f;
constexpr float ButtonPressMs   = 120.0f;
constexpr float ToggleTimeMs    = 150.0f;
constexpr float VuFpsTarget     = 60.0f;
constexpr float PeakDecayTimeMs = 250.0f;

} // namespace DesignTokens

#endif // DESIGN_TOKENS_H
