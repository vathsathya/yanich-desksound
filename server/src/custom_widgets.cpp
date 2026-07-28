#define NOMINMAX
#include "../include/custom_widgets.h"
#include "../include/DesignTokens.h"
#include "../thirdparty/imgui/imgui_internal.h"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <unordered_map>

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
    ImGui::PushStyleColor(ImGuiCol_Button, CardElevated);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.28f, 0.40f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentPrimary);
    ImGui::PushStyleColor(ImGuiCol_Text, TextPrimary);
    
    bool res = ImGui::Button(label, size);
    
    ImGui::PopStyleColor(4);
    return res;
}

bool DrawServerButton(bool isRunning, float width, float height) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    if (width <= 0.0f) {
        width = ImGui::GetContentRegionAvail().x;
    }
    if (height <= 0.0f) {
        height = ServerBtnHeight;
    }

    ImGuiContext& g = *GImGui;
    const ImGuiID id = window->GetID(isRunning ? "##StopServerBtn" : "##StartServerBtn");

    ImVec2 pos = window->DC.CursorPos;
    ImVec2 size = ImVec2(width, height);

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

bool ModernSlider(const char* label, const char* idStr, float* v, float v_min, float v_max, float default_val, const char* format, ImVec4 activeColor) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::CalcItemWidth();

    bool changed = ImGui::SliderFloat(idStr, v, v_min, v_max, format);
    
    // Custom filled accent progress track
    if (width > 0.0f) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        float grabH = ImGui::GetFrameHeight();
        float norm = (*v - v_min) / (v_max - v_min);
        norm = (std::max)(0.0f, (std::min)(1.0f, norm));
        
        float trackH = 6.0f;
        float trackY = pos.y + (grabH - trackH) * 0.5f;
        float fillW = std::max(4.0f, width * norm);
        
        ImU32 colFill = ImGui::ColorConvertFloat4ToU32(activeColor);
        drawList->AddRectFilled(ImVec2(pos.x + 2.0f, trackY), ImVec2(pos.x + fillW, trackY + trackH), colFill, 3.0f);
    }

    // Double click to reset slider to default value
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        *v = default_val;
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Double-click to reset %s", label);
    }
    return changed;
}

bool VolumeSlider(const char* label, const char* idStr, float* v, float v_min, float v_max, float default_val, float width, ImVec4 activeColor, bool isMuted) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiID id = window->GetID(idStr);

    float height = 24.0f;
    if (width <= 0.0f) width = ImGui::CalcItemWidth();

    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + width, pos.y + height));

    ImGui::ItemSize(bb, g.Style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, ImGuiButtonFlags_None);

    if (held) {
        float mouseX = g.IO.MousePos.x;
        float norm = (mouseX - pos.x) / width;
        norm = (std::max)(0.0f, (std::min)(1.0f, norm));
        *v = v_min + norm * (v_max - v_min);
        ImGui::MarkItemEdited(id);
    }

    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        *v = default_val;
        ImGui::MarkItemEdited(id);
    }

    float norm = (*v - v_min) / (v_max - v_min);
    norm = (std::max)(0.0f, (std::min)(1.0f, norm));

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    ImVec4 effectiveActiveColor = isMuted ? ImVec4(0.50f, 0.22f, 0.30f, 0.85f) : activeColor;

    // 1. Sleek 6px Dark Trough Track
    float trackH = 6.0f;
    float trackY = pos.y + (height - trackH) * 0.5f;
    ImU32 colTrough = ImGui::ColorConvertFloat4ToU32(ImVec4(0.12f, 0.16f, 0.24f, 1.00f));
    drawList->AddRectFilled(ImVec2(pos.x, trackY), ImVec2(pos.x + width, trackY + trackH), colTrough, 3.0f);

    // 2. Vibrant Filled Active Track
    float fillW = (std::max)(4.0f, width * norm);
    ImU32 colActive = ImGui::ColorConvertFloat4ToU32(effectiveActiveColor);
    drawList->AddRectFilled(ImVec2(pos.x, trackY), ImVec2(pos.x + fillW, trackY + trackH), colActive, 3.0f);

    if (hovered) {
        ImVec4 glowCol = effectiveActiveColor;
        glowCol.w = 0.25f;
        drawList->AddRectFilled(ImVec2(pos.x - 1.0f, trackY - 1.0f), ImVec2(pos.x + fillW + 1.0f, trackY + trackH + 1.0f), ImGui::ColorConvertFloat4ToU32(glowCol), 4.0f);
    }

    // 3. Smooth Pill/Circle Grab Thumb Knob with Hover Scale
    float thumbR = (hovered || held) ? 8.5f : 7.0f;
    float thumbX = pos.x + norm * width;
    thumbX = (std::max)(pos.x + thumbR, (std::min)(pos.x + width - thumbR, thumbX));
    float thumbY = pos.y + height * 0.5f;

    ImU32 colThumb = ImGui::ColorConvertFloat4ToU32(isMuted ? ImVec4(0.75f, 0.55f, 0.60f, 1.0f) : TextPrimary);
    if (hovered || held) {
        drawList->AddCircleFilled(ImVec2(thumbX, thumbY), thumbR + 3.0f, ImGui::ColorConvertFloat4ToU32(ImVec4(effectiveActiveColor.x, effectiveActiveColor.y, effectiveActiveColor.z, 0.40f)));
    }
    drawList->AddCircleFilled(ImVec2(thumbX, thumbY), thumbR, colThumb);

    if (hovered) {
        ImGui::SetTooltip("%s: %.0f%%%s (Double-click to reset)", label, *v, isMuted ? " [MUTED]" : "");
    }

    return held || pressed;
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

#include <unordered_map>

bool IPChip(const char* labelId, const std::string& ipStr) {
    ImGui::PushID(labelId);
    
    static std::unordered_map<std::string, float> copyTimers;
    float currentTime = (float)ImGui::GetTime();
    bool isRecentlyCopied = (copyTimers.find(labelId) != copyTimers.end()) && (currentTime - copyTimers[labelId] < 1.5f);

    std::string textLabel = isRecentlyCopied ? (ipStr + "  [Copied!]") : (ipStr + "  [Copy]");
    ImVec2 textSize = ImGui::CalcTextSize(textLabel.c_str());
    ImVec2 chipSize = ImVec2(textSize.x + 20.0f, 32.0f);
    
    if (isRecentlyCopied) {
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSecondary);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentSecondary);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentSecondary);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, CardElevated);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.28f, 0.40f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentPrimary);
    }

    bool clicked = ImGui::Button(textLabel.c_str(), ImVec2(chipSize.x, chipSize.y));
    if (clicked) {
        CopyToClipboardWin(ipStr);
        copyTimers[labelId] = currentTime;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(isRecentlyCopied ? "Copied to clipboard!" : "Click to copy %s", ipStr.c_str());
    }
    
    ImGui::PopStyleColor(3);
    ImGui::PopID();
    return clicked;
}

void LEDVuMeter(const char* label, float currentLevel, float& smoothLevel, float& peakHoldLevel, ImVec4 baseColor, int segments, float customWidth) {
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
    ImGui::SameLine(50.0f);

    float labelW = 50.0f;
    float availW = (customWidth > 0.0f) ? (customWidth - labelW) : (ImGui::GetContentRegionAvail().x - labelW);
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

    // Highly visible inactive LED block track (#25354D) with subtle border
    ImU32 colInactive = ImGui::ColorConvertFloat4ToU32(ImVec4(0.15f, 0.21f, 0.30f, 1.00f));
    ImU32 colInactiveBorder = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
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
        ImU32 colGlow = ImGui::ColorConvertFloat4ToU32(ImVec4(segColor.x, segColor.y, segColor.z, 0.30f));

        if (i < activeSegments) {
            drawList->AddRectFilled(ImVec2(x0 - 0.5f, y0 - 0.5f), ImVec2(x1 + 0.5f, y1 + 0.5f), colGlow, 3.0f);
            drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), colActive, 2.5f);
        } else {
            // Draw visible dark slate inactive track block
            drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), colInactive, 2.5f);
            drawList->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), colInactiveBorder, 2.5f);
        }

        if (i == peakSegment && peakSegment > activeSegments && peakHoldLevel > 0.02f) {
            drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), colPeak, 2.5f);
        }
    }

    ImGui::ItemSize(ImVec2(availW, meterH));

    ImGui::SameLine();
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

    // Centered Phone + WiFi Outline Watermark (10% Opacity)
    ImVec2 center = ImVec2(pos.x + containerSize.x * 0.5f, pos.y + containerSize.y * 0.30f);
    ImU32 watermarkCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.58f, 0.64f, 0.72f, 0.12f));

    // Phone Outline
    drawList->AddRect(ImVec2(center.x - 14.0f, center.y - 20.0f), ImVec2(center.x + 14.0f, center.y + 20.0f), watermarkCol, 4.0f, 0, 2.0f);
    drawList->AddCircleFilled(ImVec2(center.x, center.y + 15.0f), 2.0f, watermarkCol);

    // WiFi Arcs Outline
    drawList->AddCircle(center, 30.0f, watermarkCol, 0, 1.5f);
    drawList->AddCircle(center, 42.0f, watermarkCol, 0, 1.5f);

    // Centered Title & Subtitle - Aligned safely within container height
    ImVec2 tSize = ImGui::CalcTextSize(title);
    ImVec2 sSize = ImGui::CalcTextSize(subtitle);

    drawList->AddText(ImVec2(center.x - tSize.x * 0.5f, pos.y + containerSize.y * 0.56f), ImGui::ColorConvertFloat4ToU32(TextPrimary), title);
    drawList->AddText(ImVec2(center.x - sSize.x * 0.5f, pos.y + containerSize.y * 0.70f), ImGui::ColorConvertFloat4ToU32(TextSecondary), subtitle);

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
    if (isRunning) {
        float pulseAlpha = 0.25f + 0.35f * (0.5f + 0.5f * sinf((float)ImGui::GetTime() * 4.5f));
        ImVec4 glowCol = dotCol;
        glowCol.w = pulseAlpha;
        drawList->AddCircleFilled(ImVec2(pos.x + 10.0f, pos.y + 11.0f), 6.0f, ImGui::ColorConvertFloat4ToU32(glowCol));
    }
    drawList->AddCircleFilled(ImVec2(pos.x + 10.0f, pos.y + 11.0f), 3.5f, ImGui::ColorConvertFloat4ToU32(dotCol));

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
    ImGui::TextColored(dotCol, isRunning ? "[RUNNING]" : "[STOPPED]");
}

void CardHeader(const char* iconSymbol, const char* title) {
    ImGui::TextColored(AccentPrimary, "%s", iconSymbol);
    ImGui::SameLine();
    ImGui::TextColored(TextPrimary, "%s", title);
    ImGui::Spacing();
}

} // namespace DesignSystem
