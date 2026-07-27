#ifndef CUSTOM_WIDGETS_H
#define CUSTOM_WIDGETS_H

#include "../include/DesignTokens.h"
#include "../thirdparty/imgui/imgui.h"
#include <string>
#include <vector>

namespace DesignSystem {

// ====================================================
// REUSABLE WIDGET LIBRARY & CARD HELPERS
// ====================================================

// Auto-Height Card Container Helpers (height: auto)
void BeginCard(const char* id, float width = 0.0f);
void EndCard();

// Buttons
bool PrimaryButton(const char* label, ImVec2 size = ImVec2(0.0f, 0.0f));
bool DangerButton(const char* label, ImVec2 size = ImVec2(0.0f, 0.0f));
bool GhostButton(const char* label, ImVec2 size = ImVec2(0.0f, 0.0f));
bool DrawServerButton(bool isRunning, float width = 0.0f);

// Controls
bool ModernSlider(const char* label, const char* id, float* v, float v_min, float v_max, float default_val, const char* format = "%.0f%%");
bool ToggleSwitch(const char* label, bool* v);
bool IPChip(const char* labelId, const std::string& ipStr);

// Audio & Telemetry
void LEDVuMeter(const char* label, float currentLevel, float& smoothLevel, float& peakHoldLevel, ImVec4 baseColor, int segments = 36);
void MetricCard(const char* title, const char* valueStr, const float* sparkValues, int count, ImVec4 color, ImVec2 cardSize);

// Feedback & Containers
void EmptyState(ImVec2 containerSize, const char* title = "No devices connected", const char* subtitle = "Open Yanich DeskSound on your mobile device.");
void StatusPill(bool isRunning);
void CardHeader(const char* iconSymbol, const char* title);
void VersionBadge(const char* versionStr);
void StatusDotBadge(bool isRunning);

} // namespace DesignSystem

#endif // CUSTOM_WIDGETS_H
