#define NOMINMAX
#include "../include/custom_widgets.h"
#include "../include/DesignTokens.h"
#include "../thirdparty/imgui/imgui_internal.h"
#include <cmath>
#include <algorithm>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
static void CopyToClipboardWin(const std::string& text) {
    if (!OpenClipboard(NULL)) return;
    EmptyClipboard();
    size_t bytes = text.length() + 1;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        void* ptr = GlobalLock(hMem);
        if (ptr) {
            memcpy(ptr, text.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_TEXT, hMem);
        }
    }
    CloseClipboard();
}
#else
static void CopyToClipboardWin(const std::string& text) {
    ImGui::SetClipboardText(text.c_str());
}
#endif

namespace DesignSystem {

using namespace DesignTokens;

bool PrimaryButton(const char* label, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button, AccentPrimary);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.38f, 0.88f, 0.98f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.70f, 0.82f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Text, BgMain);
    
    bool res = ImGui::Button(label, size);
    
    ImGui::PopStyleColor(4);
    return res;
}

bool DangerButton(const char* label, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button, ColorDanger);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.98f, 0.35f, 0.35f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.80f, 0.20f, 0.20f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Text, TextPrimary);
    
    bool res = ImGui::Button(label, size);
    
    ImGui::PopStyleColor(4);
    return res;
}

bool GhostButton(const char* label, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, CardElevated);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.28f, 0.40f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Text, TextSecondary);
    
    bool res = ImGui::Button(label, size);
    
    ImGui::PopStyleColor(4);
    return res;
}

bool DrawServerButton(bool isRunning, float width) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    if (width <= 0.0f) {
        width = ImGui::GetContentRegionAvail().x;
    }

    ImGuiContext& g = *GImGui;
    const ImGuiID id = window->GetID(isRunning ? "##StopServerBtn" : "##StartServerBtn");

    ImVec2 pos = window->DC.CursorPos;
    ImVec2 size = ImVec2(width, ServerBtnHeight);

    const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
    ImGui::ItemSize(bb, g.Style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    ImVec4 baseCol = isRunning ? ColorDanger : AccentSecondary;
    if (hovered) {
        baseCol.x = (std::min)(1.0f, baseCol.x * 1.12f);
        baseCol.y = (std::min)(1.0f, baseCol.y * 1.12f);
        baseCol.z = (std::min)(1.0f, baseCol.z * 1.12f);
    }
    if (held) {
        baseCol.w = 0.85f;
    }

    ImU32 u32Btn = ImGui::ColorConvertFloat4ToU32(baseCol);
    drawList->AddRectFilled(bb.Min, bb.Max, u32Btn, ButtonRadius);

    if (hovered) {
        ImVec4 glowCol = baseCol;
        glowCol.w = 0.20f;
        drawList->AddRectFilled(ImVec2(bb.Min.x - 2.0f, bb.Min.y - 2.0f), ImVec2(bb.Max.x + 2.0f, bb.Max.y + 2.0f), ImGui::ColorConvertFloat4ToU32(glowCol), ButtonRadius + 2.0f);
    }

    const char* text = isRunning ? "[P] Stop Server" : "[P] Start Server";
    ImVec2 textSize = ImGui::CalcTextSize(text);
    ImVec2 textPos = ImVec2(pos.x + (size.x - textSize.x) * 0.5f, pos.y + (size.y - textSize.y) * 0.5f);

    drawList->AddText(textPos, ImGui::ColorConvertFloat4ToU32(TextPrimary), text);

    return pressed;
}

bool ModernSlider(const char* label, const char* idStr, float* v, float v_min, float v_max, float default_val, const char* format) {
    bool changed = ImGui::SliderFloat(idStr, v, v_min, v_max, format);
    
    // Double click to reset slider to default value
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        *v = default_val;
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Double-click to reset (%s)", label);
    }
    return changed;
}

bool ToggleSwitch(const char* label, bool* v) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);

    float height = ToggleSwitchH;
    float width = ToggleSwitchW;
    float radius = height * 0.5f;

    const ImVec2 pos = window->DC.CursorPos;
    const ImRect total_bb(pos, ImVec2(pos.x + width + (label_size.x > 0 ? g.Style.ItemInnerSpacing.x + label_size.x : 0.0f), pos.y + ImGui::GetFrameHeight()));
    
    ImGui::ItemSize(total_bb, g.Style.FramePadding.y);
    if (!ImGui::ItemAdd(total_bb, id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(total_bb, id, &hovered, &held);
    if (pressed) {
        *v = !(*v);
        ImGui::MarkItemEdited(id);
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    
    ImVec4 bgCol = *v ? AccentPrimary : ImVec4(0.18f, 0.24f, 0.35f, 1.00f);
    if (hovered) {
        bgCol.w = 0.90f;
    }
    
    ImU32 u32Bg = ImGui::ColorConvertFloat4ToU32(bgCol);
    drawList->AddRectFilled(ImVec2(pos.x, pos.y + 2.0f), ImVec2(pos.x + width, pos.y + height + 2.0f), u32Bg, radius);

    float circleX = *v ? (pos.x + width - radius) : (pos.x + radius);
    float circleY = pos.y + 2.0f + radius;
    drawList->AddCircleFilled(ImVec2(circleX, circleY), radius - 2.5f, ImGui::ColorConvertFloat4ToU32(TextPrimary));

    if (label_size.x > 0.0f) {
        ImGui::RenderText(ImVec2(pos.x + width + g.Style.ItemInnerSpacing.x, pos.y + 2.0f), label);
    }

    return pressed;
}

bool IPChip(const char* labelId, const std::string& ipStr) {
    ImGui::PushID(labelId);
    
    std::string textLabel = ipStr + "  [Copy]";
    ImVec2 textSize = ImGui::CalcTextSize(textLabel.c_str());
    ImVec2 chipSize = ImVec2(textSize.x + 20.0f, 32.0f);
    
    ImGui::PushStyleColor(ImGuiCol_Button, CardElevated);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.28f, 0.40f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentPrimary);

    bool clicked = ImGui::Button(textLabel.c_str(), ImVec2(chipSize.x, chipSize.y));
    if (clicked) {
        CopyToClipboardWin(ipStr);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Click to copy %s", ipStr.c_str());
    }
    
    ImGui::PopStyleColor(3);
    ImGui::PopID();
    return clicked;
}

void LEDVuMeter(const char* label, float currentLevel, float& smoothLevel, float& peakHoldLevel, ImVec4 baseColor, int segments) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;

    float dt = ImGui::GetIO().DeltaTime;
    smoothLevel += (currentLevel - smoothLevel) * (std::min)(1.0f, dt * 28.0f);
    
    if (smoothLevel > peakHoldLevel) {
        peakHoldLevel = smoothLevel;
    } else {
        peakHoldLevel = (std::max)(0.0f, peakHoldLevel - dt * 0.80f);
    }

    ImGui::TextColored(TextSecondary, "%s", label);
    ImGui::SameLine(60.0f);

    float availW = ImGui::GetContentRegionAvail().x - 65.0f;
    float meterH = 14.0f;
    ImVec2 pos = ImGui::GetCursorScreenPos();
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    
    float clampedLevel = std::max(0.0f, std::min(1.0f, smoothLevel));
    float clampedPeak = std::max(0.0f, std::min(1.0f, peakHoldLevel));

    int activeSegments = (int)std::round(clampedLevel * segments);
    int peakSegment = (int)std::round(clampedPeak * segments);

    float gap = 2.0f;
    float totalGaps = gap * (segments - 1);
    float blockW = std::max(2.0f, (availW - totalGaps) / (float)segments);

    ImU32 colInactive = ImGui::ColorConvertFloat4ToU32(ImVec4(0.12f, 0.16f, 0.24f, 0.70f));
    ImU32 colPeak = ImGui::ColorConvertFloat4ToU32(ImVec4(0.98f, 0.98f, 0.99f, 0.95f));

    for (int i = 0; i < segments; ++i) {
        float x0 = pos.x + i * (blockW + gap);
        float y0 = pos.y;
        float x1 = x0 + blockW;
        float y1 = y0 + meterH;

        float ratio = (float)i / (float)segments;

        ImVec4 segColor = baseColor;
        if (ratio >= 0.95f) {
            segColor = ColorDanger;
        } else if (ratio >= 0.80f) {
            segColor = ColorWarning;
        }

        ImU32 colActive = ImGui::ColorConvertFloat4ToU32(segColor);
        ImU32 colGlow = ImGui::ColorConvertFloat4ToU32(ImVec4(segColor.x, segColor.y, segColor.z, 0.25f));

        if (i < activeSegments) {
            drawList->AddRectFilled(ImVec2(x0 - 0.5f, y0 - 0.5f), ImVec2(x1 + 0.5f, y1 + 0.5f), colGlow, 3.0f);
            drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), colActive, 2.5f);
        } else if (i == peakSegment && peakSegment > 0) {
            drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), colPeak, 2.5f);
        } else {
            drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), colInactive, 2.5f);
        }
    }

    ImGui::ItemSize(ImVec2(availW, meterH));

    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 55.0f);
    ImGui::TextColored(baseColor, "%3d%%", (int)(clampedLevel * 100.0f));
}

void MetricCard(const char* title, const char* valueStr, const float* sparkValues, int count, ImVec4 color, ImVec2 cardSize) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;

    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    drawList->AddRectFilled(pos, ImVec2(pos.x + cardSize.x, pos.y + cardSize.y), ImGui::ColorConvertFloat4ToU32(CardElevated), FrameRadius);
    drawList->AddRect(pos, ImVec2(pos.x + cardSize.x, pos.y + cardSize.y), ImGui::ColorConvertFloat4ToU32(BorderColor), FrameRadius);

    drawList->AddCircleFilled(ImVec2(pos.x + 12.0f, pos.y + 14.0f), 3.0f, ImGui::ColorConvertFloat4ToU32(color));
    drawList->AddText(ImVec2(pos.x + 20.0f, pos.y + 7.0f), ImGui::ColorConvertFloat4ToU32(TextSecondary), title);

    drawList->AddText(ImVec2(pos.x + 12.0f, pos.y + 24.0f), ImGui::ColorConvertFloat4ToU32(TextPrimary), valueStr);

    if (sparkValues && count > 1) {
        float graphY0 = pos.y + 48.0f;
        float graphH = cardSize.y - 52.0f;
        float minVal = sparkValues[0], maxVal = sparkValues[0];
        for (int i = 1; i < count; ++i) {
            if (sparkValues[i] < minVal) minVal = sparkValues[i];
            if (sparkValues[i] > maxVal) maxVal = sparkValues[i];
        }
        if (maxVal - minVal < 0.0001f) maxVal = minVal + 1.0f;

        float dx = (cardSize.x - 24.0f) / (float)(count - 1);
        ImU32 colLine = ImGui::ColorConvertFloat4ToU32(color);

        for (int i = 0; i < count - 1; ++i) {
            float n1 = (sparkValues[i] - minVal) / (maxVal - minVal);
            float n2 = (sparkValues[i + 1] - minVal) / (maxVal - minVal);

            float x1 = pos.x + 12.0f + i * dx;
            float y1 = graphY0 + graphH - n1 * graphH;
            float x2 = pos.x + 12.0f + (i + 1) * dx;
            float y2 = graphY0 + graphH - n2 * graphH;

            drawList->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), colLine, 1.6f);
        }
    }

    ImGui::ItemSize(cardSize);
}

void EmptyState(ImVec2 containerSize, const char* title, const char* subtitle) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    ImVec2 center = ImVec2(pos.x + containerSize.x * 0.5f, pos.y + containerSize.y * 0.35f);
    ImU32 watermarkCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.58f, 0.64f, 0.72f, 0.12f));

    drawList->AddRect(ImVec2(center.x - 14.0f, center.y - 20.0f), ImVec2(center.x + 14.0f, center.y + 20.0f), watermarkCol, 4.0f, 0, 2.0f);
    drawList->AddCircleFilled(ImVec2(center.x, center.y + 15.0f), 2.0f, watermarkCol);

    drawList->AddCircle(center, 32.0f, watermarkCol, 0, 1.5f);
    drawList->AddCircle(center, 44.0f, watermarkCol, 0, 1.5f);

    ImVec2 tSize = ImGui::CalcTextSize(title);
    ImVec2 sSize = ImGui::CalcTextSize(subtitle);

    drawList->AddText(ImVec2(center.x - tSize.x * 0.5f, pos.y + containerSize.y * 0.65f), ImGui::ColorConvertFloat4ToU32(TextPrimary), title);
    drawList->AddText(ImVec2(center.x - sSize.x * 0.5f, pos.y + containerSize.y * 0.78f), ImGui::ColorConvertFloat4ToU32(TextSecondary), subtitle);

    ImGui::ItemSize(containerSize);
}

void StatusPill(bool isRunning) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    ImVec4 bgCol = isRunning ? ImVec4(0.06f, 0.73f, 0.49f, 0.15f) : ImVec4(0.94f, 0.27f, 0.27f, 0.15f);
    ImVec4 dotCol = isRunning ? AccentSecondary : ColorDanger;
    const char* text = isRunning ? "RUNNING" : "STOPPED";

    ImVec2 textSize = ImGui::CalcTextSize(text);
    float badgeW = textSize.x + 28.0f;
    float badgeH = 22.0f;

    drawList->AddRectFilled(pos, ImVec2(pos.x + badgeW, pos.y + badgeH), ImGui::ColorConvertFloat4ToU32(bgCol), PillRadius);
    drawList->AddCircleFilled(ImVec2(pos.x + 10.0f, pos.y + 11.0f), 4.0f, ImGui::ColorConvertFloat4ToU32(dotCol));

    ImGui::SetCursorScreenPos(ImVec2(pos.x + 18.0f, pos.y + 3.0f));
    ImGui::TextColored(dotCol, "%s", text);
    
    ImGui::SetCursorScreenPos(ImVec2(pos.x + badgeW + 8.0f, pos.y));
}

void VersionBadge(const char* versionStr) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.20f, 0.30f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Text, AccentPrimary);
    ImGui::Button(versionStr, ImVec2(55.0f, 20.0f));
    ImGui::PopStyleColor(2);
}

void StatusDotBadge(bool isRunning) {
    ImVec4 dotCol = isRunning ? AccentSecondary : ColorDanger;
    ImGui::TextColored(dotCol, isRunning ? "● RUNNING" : "● STOPPED");
}

void CardHeader(const char* iconSymbol, const char* title) {
    ImGui::TextColored(AccentPrimary, "%s", iconSymbol);
    ImGui::SameLine();
    ImGui::TextColored(TextPrimary, "%s", title);
    ImGui::Spacing();
}

} // namespace DesignSystem
