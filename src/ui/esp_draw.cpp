// Projects the ESP targets collected by the game thread onto the screen.
// Uses Unreal Engine 3 conventions: X forward, Y right, Z up; rotators in
// 65536 units per turn; horizontal field of view.
#include "pages.h"

#include "../core/strutil.h"

#include <cmath>

namespace omm::ui {

using namespace game;

namespace {
struct Camera {
    ue3::FVector loc, x, y, z;
    float focal = 1.f;  // pixels per unit at depth 1
    ImVec2 center;
};

Camera MakeCamera(const Snapshot& snap, const ImVec2& display) {
    Camera c;
    c.loc = snap.camLocation;
    const float k = ue3::kRotToRad;
    float sp = std::sin(snap.camRotation.Pitch * k), cp = std::cos(snap.camRotation.Pitch * k);
    float sy = std::sin(snap.camRotation.Yaw * k), cy = std::cos(snap.camRotation.Yaw * k);
    float sr = std::sin(snap.camRotation.Roll * k), cr = std::cos(snap.camRotation.Roll * k);
    c.x = ue3::FVector(cp * cy, cp * sy, sp);
    c.y = ue3::FVector(sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp);
    c.z = ue3::FVector(-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp);
    c.center = ImVec2(display.x * 0.5f, display.y * 0.5f);
    float fov = Clamp(snap.camFov, 10.f, 170.f);
    c.focal = c.center.x / std::tan(fov * 3.14159265f / 360.f);
    return c;
}

// Returns false when the point is behind the camera; `out` is still set to
// a direction usable for edge arrows.
bool Project(const Camera& c, const ue3::FVector& p, ImVec2& out) {
    ue3::FVector d = p - c.loc;
    float tx = d.Dot(c.y), ty = d.Dot(c.z), tz = d.Dot(c.x);
    if (tz < 1.f) {
        out = ImVec2(tx, -ty);  // direction only
        return false;
    }
    out = ImVec2(c.center.x + tx * c.focal / tz, c.center.y - ty * c.focal / tz);
    return true;
}

ImU32 Col(const float* rgba, float alphaMul = 1.f) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(rgba[0], rgba[1], rgba[2], rgba[3] * alphaMul));
}

void TextShadow(ImDrawList* dl, ImVec2 pos, ImU32 col, const char* text) {
    dl->AddText(ImVec2(pos.x + 1, pos.y + 1), IM_COL32(0, 0, 0, 200), text);
    dl->AddText(pos, col, text);
}

void EdgeArrow(ImDrawList* dl, const Camera& c, const ImVec2& display, ImVec2 dir, ImU32 col) {
    float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    if (len < 0.001f) return;
    dir.x /= len;
    dir.y /= len;
    float r = std::min(display.x, display.y) * 0.42f;
    ImVec2 tip(c.center.x + dir.x * r, c.center.y + dir.y * r);
    ImVec2 side(-dir.y, dir.x);
    float s = Em(0.7f);
    dl->AddTriangleFilled(tip, ImVec2(tip.x - dir.x * s * 1.6f + side.x * s, tip.y - dir.y * s * 1.6f + side.y * s),
                          ImVec2(tip.x - dir.x * s * 1.6f - side.x * s, tip.y - dir.y * s * 1.6f - side.y * s), col);
}
}  // namespace

void DrawEsp(const Snapshot& snap, const ModState& s) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    if (display.x < 2 || display.y < 2) return;
    Camera cam = MakeCamera(snap, display);
    const float m = 0.02f;  // metres per unit (approx.)

    for (const EspItem& it : snap.esp) {
        const EspStyle& st = s.esp[static_cast<int>(it.category)];
        if (!st.enabled) continue;
        float alpha = it.collected ? 0.35f : 1.f;
        ImU32 col = Col(st.color, alpha);
        ImVec2 p;
        bool onScreen = Project(cam, it.location, p);
        if (onScreen) onScreen = p.x >= -50 && p.x <= display.x + 50 && p.y >= -50 && p.y <= display.y + 50;
        if (!onScreen) {
            if (s.espOffscreenArrows && (it.category == EspCategory::Enemy || it.distance < 2500.f)) {
                ImVec2 dir;
                if (Project(cam, it.location, dir)) dir = ImVec2(dir.x - cam.center.x, dir.y - cam.center.y);
                EdgeArrow(dl, cam, display, dir, col);
            }
            continue;
        }

        std::string text;
        if (s.espShowLabels) text = it.label;
        if (s.espShowDistance) text += str::Format(text.empty() ? "%.0fm" : " [%.0fm]", it.distance * m);

        if (it.height > 0.f) {
            // Box around a character.
            ImVec2 top, bottom;
            ue3::FVector up = it.location + ue3::FVector(0, 0, it.height);
            ue3::FVector down = it.location - ue3::FVector(0, 0, it.height);
            if (Project(cam, up, top) && Project(cam, down, bottom)) {
                float h = bottom.y - top.y;
                float w = h * 0.42f;
                ImVec2 a(p.x - w * 0.5f, top.y), b(p.x + w * 0.5f, bottom.y);
                dl->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(b.x + 1, b.y + 1), IM_COL32(0, 0, 0, 160), 0, 0, 3.f);
                dl->AddRect(a, b, col, 0, 0, 1.5f);
                if (!text.empty()) {
                    ImVec2 ts = ImGui::CalcTextSize(text.c_str());
                    TextShadow(dl, ImVec2(p.x - ts.x * 0.5f, a.y - ts.y - 2), col, text.c_str());
                }
                if (s.espShowLabels && !it.detail.empty()) {
                    ImVec2 ts = ImGui::CalcTextSize(it.detail.c_str());
                    TextShadow(dl, ImVec2(p.x - ts.x * 0.5f, b.y + 2), col, it.detail.c_str());
                }
                if (s.espTracers) dl->AddLine(ImVec2(display.x * 0.5f, display.y), ImVec2(p.x, b.y), col, 1.f);
                continue;
            }
        }
        // Point marker.
        float r = Em(0.32f);
        dl->AddCircleFilled(p, r + 1.5f, IM_COL32(0, 0, 0, 170), 12);
        dl->AddCircleFilled(p, r, col, 12);
        if (!text.empty()) TextShadow(dl, ImVec2(p.x + r + 4, p.y - Em(0.5f)), col, text.c_str());
        if (s.espShowLabels && !it.detail.empty())
            TextShadow(dl, ImVec2(p.x + r + 4, p.y + Em(0.45f)), Col(st.color, alpha * 0.75f), it.detail.c_str());
        if (s.espTracers) dl->AddLine(ImVec2(display.x * 0.5f, display.y), p, Col(st.color, alpha * 0.6f), 1.f);
    }
}

}  // namespace omm::ui
