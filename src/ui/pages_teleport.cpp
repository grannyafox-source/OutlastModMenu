// Teleport page: saved positions, story checkpoints, scenes and roulette.
#include "pages.h"

#include "../core/strutil.h"
#include "../game/actions.h"
#include "../game/data/checkpoints.h"
#include "../game/outlast.h"

namespace omm::ui {

using namespace game;

namespace {
char g_label[64] = "";
bool g_randMain = true;
bool g_randDlc = false;

void LoadButton(const char* checkpoint, const char* label) {
    if (ImGui::SmallButton(label)) {
        std::string cp = checkpoint;
        Enqueue([cp] { actions::LoadCheckpoint(cp); });
    }
}
}  // namespace

void PageTeleport(Ctx& c) {
    const Snapshot& snap = c.snap;
    ModState& s = c.s;
    ImGui::Text("Level: %s", snap.mapName.empty() ? "-" : snap.mapName.c_str());
    ImGui::SameLine(0, Em(2));
    ImGui::Text("Checkpoint: %s", snap.checkpoint.empty() ? "-" : snap.checkpoint.c_str());

    Heading("Positions in this level");
    ImGui::BeginDisabled(!snap.inGame);
    if (ImGui::Button("Teleport to crosshair")) Enqueue([] { actions::TeleportToCrosshair(); });
    ImGui::SameLine();
    if (ImGui::Button("Random spot in this level")) Enqueue([] { actions::TeleportRandomLocation(); });
    ImGui::SameLine();
    if (ImGui::Button("Quick save")) Enqueue([] { actions::QuickSavePosition(); });
    ImGui::SameLine();
    if (ImGui::Button("Quick load")) Enqueue([] { actions::QuickLoadPosition(); });
    ImGui::SetNextItemWidth(Em(14));
    ImGui::InputTextWithHint("##label", "name, e.g. 'Before the sewers'", g_label, sizeof(g_label));
    ImGui::SameLine();
    if (ImGui::Button("Save current position")) {
        std::string label = g_label[0] ? g_label : "Position";
        Enqueue([label] { actions::SavePosition(label); });
        g_label[0] = 0;
    }
    ImGui::EndDisabled();
    std::vector<actions::SavedPosition> saved = actions::SavedPositions(snap.mapName);
    if (saved.empty()) {
        Hint("No saved positions in this level yet.");
    } else {
        for (const actions::SavedPosition& p : saved) {
            ImGui::PushID(p.key.c_str());
            ImGui::BulletText("%s", p.label.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("(%.0f, %.0f, %.0f)", p.location.X, p.location.Y, p.location.Z);
            ImGui::SameLine();
            if (ImGui::SmallButton("Go")) {
                actions::SavedPosition copy = p;
                Enqueue([copy] { actions::GoToPosition(copy); });
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete")) actions::DeletePosition(p.key);
            ImGui::PopID();
        }
    }

    Heading("Collectibles in this area");
    if (!s.espEnabled || !s.esp[static_cast<int>(EspCategory::Document)].enabled ||
        !s.esp[static_cast<int>(EspCategory::Recording)].enabled) {
        Hint("The list uses the ESP scanner.");
        if (ImGui::Button("Turn on ESP for documents and recording spots")) {
            s.espEnabled = true;
            s.esp[static_cast<int>(EspCategory::Document)].enabled = true;
            s.esp[static_cast<int>(EspCategory::Recording)].enabled = true;
        }
    } else {
        int shown = 0;
        for (const EspItem& it : snap.esp) {
            if (it.category != EspCategory::Document && it.category != EspCategory::Recording &&
                it.category != EspCategory::Battery)
                continue;
            ImGui::PushID(shown++);
            if (ImGui::SmallButton("Go")) {
                ue3::FVector dest = it.location + ue3::FVector(0, 0, 60);
                Enqueue([dest] { TeleportPlayer(dest, nullptr); });
            }
            ImGui::SameLine();
            ImGui::Text("%s%s%s  (%.0f m)%s", it.label.c_str(), it.detail.empty() ? "" : " - ", it.detail.c_str(),
                        it.distance * 0.02f, it.collected ? "  [done]" : "");
            ImGui::PopID();
        }
        if (!shown) Hint("Nothing within the ESP distance (raise 'Max distance' on the ESP page to see more).");
    }

    Heading("Random");
    ImGui::Checkbox("Outlast", &g_randMain);
    ImGui::SameLine();
    ImGui::Checkbox("Whistleblower (DLC)", &g_randDlc);
    if (ImGui::Button("Random checkpoint")) {
        std::string cp = actions::RandomCheckpoint(g_randMain, g_randDlc);
        if (!cp.empty()) Enqueue([cp] { actions::LoadCheckpoint(cp); });
    }
    ImGui::SameLine();
    if (ImGui::Button("Random scene")) {
        std::string cp = actions::RandomScene(g_randMain, g_randDlc);
        if (!cp.empty()) Enqueue([cp] { actions::LoadCheckpoint(cp); });
    }
    const char* modes[] = {"Off", "Random spot in the level", "Random checkpoint", "Random scene"};
    ImGui::Combo("Automatic roulette", &s.rouletteMode, modes, 4);
    Help("Jumps somewhere random on its own, over and over.");
    if (s.rouletteMode) {
        SliderF("Every (seconds)", &s.rouletteInterval, 10.f, 900.f, "%.0f", 120.f);
        ImGui::Checkbox("Include Outlast", &s.rouletteIncludeMain);
        ImGui::SameLine();
        ImGui::Checkbox("Include Whistleblower", &s.rouletteIncludeDlc);
    }

    Heading("Scenes");
    if (ImGui::BeginTable("##scenes", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Scene", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("What happens", ImGuiTableColumnFlags_WidthStretch, 2.6f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, Em(3.5f));
        for (const data::Scene& sc : data::Scenes()) {
            ImGui::PushID(sc.checkpoint);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s%s", sc.title, sc.dlc ? " (DLC)" : "");
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", sc.description);
            ImGui::TableNextColumn();
            LoadButton(sc.checkpoint, "Play");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    Heading("Chapters and checkpoints");
    Hint("Loading starts the game from that checkpoint without touching your save. Note that the game autosaves "
         "again when you reach the next checkpoint.");
    for (const data::Chapter& ch : data::Chapters()) {
        std::string title = std::string(ch.dlc ? "[Whistleblower] " : "") + ch.title;
        if (!ImGui::CollapsingHeader(title.c_str())) continue;
        ImGui::PushID(title.c_str());
        for (const data::Checkpoint& cp : ch.checkpoints) {
            ImGui::PushID(cp.name);
            LoadButton(cp.name, "Load");
            ImGui::SameLine();
            ImGui::TextUnformatted(cp.name);
            if (cp.note) {
                ImGui::SameLine();
                ImGui::TextDisabled("- %s", cp.note);
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    std::vector<std::string> fromGame = actions::GameCheckpointList();
    if (!fromGame.empty() && ImGui::CollapsingHeader("Checkpoints listed by the current level")) {
        for (const std::string& cp : fromGame) {
            ImGui::PushID(cp.c_str());
            LoadButton(cp.c_str(), "Load");
            ImGui::SameLine();
            ImGui::TextUnformatted(cp.c_str());
            ImGui::PopID();
        }
    }
}

}  // namespace omm::ui
