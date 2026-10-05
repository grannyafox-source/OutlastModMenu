// World & AI and Enemies pages.
#include "pages.h"

#include "../core/strutil.h"
#include "../game/enemies.h"

#include <algorithm>

namespace omm::ui {

using namespace game;

namespace {
enemies::SpawnRequest g_spawn;
char g_meshFilter[64] = "";
int g_meshTargetEnemy = -1;
}  // namespace

void PageWorld(Ctx& c) {
    ModState& s = c.s;
    Heading("Game speed");
    Toggle("Change game speed", &s.gameSpeedOverride, "Slows down or speeds up the whole game (everything but the menu).");
    ImGui::BeginDisabled(!s.gameSpeedOverride);
    SliderF("Speed", &s.gameSpeed, 0.05f, 5.f, "%.2fx", 1.f);
    const float speeds[] = {0.1f, 0.25f, 0.5f, 1.f, 2.f, 4.f};
    for (float v : speeds) {
        if (ImGui::SmallButton(str::Format("%gx", v).c_str())) s.gameSpeed = v;
        ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::EndDisabled();
    ImGui::TextDisabled("Current time dilation: %.2f", c.snap.timeDilation);

    Heading("Enemy AI (all enemies)");
    Toggle("Freeze all enemies", &s.freezeEnemies);
    Toggle("Enemy speed of time", &s.enemyTimeScaleOverride, "Enemies move in slow motion (or fast-forward) while you don't.");
    if (s.enemyTimeScaleOverride) SliderF("Enemy time scale", &s.enemyTimeScale, 0.05f, 3.f, "%.2fx", 1.f);
    Toggle("Passive enemies", &s.passiveEnemies, "Enemies still walk around and chase you but never attack.");
    Toggle("Blind enemies", &s.blindEnemies, "Enemies can't see you.");
    Toggle("Deaf enemies", &s.deafEnemies, "Enemies can't hear you.");
    Toggle("Enemy movement speed", &s.enemySpeedOverride);
    if (s.enemySpeedOverride) SliderF("Enemy speed", &s.enemySpeedMultiplier, 0.1f, 4.f, "%.2fx", 1.f);
    Toggle("Enemy damage", &s.enemyDamageOverride);
    if (s.enemyDamageOverride) SliderF("Damage multiplier", &s.enemyDamageMultiplier, 0.f, 10.f, "%.2fx", 1.f);
    Toggle("Invisible enemies", &s.invisibleEnemies, "Hides enemy models (they are still there - good luck).");
    Toggle("Resize enemies", &s.enemyScaleOverride);
    if (s.enemyScaleOverride) {
        ImGui::SliderFloat3("Scale (X, Y, Z)", s.enemyScale, 0.2f, 4.f, "%.2f");
        if (ImGui::SmallButton("Tiny")) s.enemyScale[0] = s.enemyScale[1] = s.enemyScale[2] = 0.4f;
        ImGui::SameLine();
        if (ImGui::SmallButton("Giant")) s.enemyScale[0] = s.enemyScale[1] = s.enemyScale[2] = 2.f;
        ImGui::SameLine();
        if (ImGui::SmallButton("Pancake")) {
            s.enemyScale[0] = s.enemyScale[1] = 1.6f;
            s.enemyScale[2] = 0.35f;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Noodle")) {
            s.enemyScale[0] = s.enemyScale[1] = 0.45f;
            s.enemyScale[2] = 1.9f;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Normal")) s.enemyScale[0] = s.enemyScale[1] = s.enemyScale[2] = 1.f;
    }
}

void PageEnemies(Ctx& c) {
    const Snapshot& snap = c.snap;
    ModState& s = c.s;
    const auto& types = enemies::SpawnTypes();

    Heading("Spawn enemies");
    if (ImGui::BeginCombo("Enemy", types[Clamp(g_spawn.type, 0, static_cast<int>(types.size()) - 1)].label)) {
        for (int i = 0; i < static_cast<int>(types.size()); ++i)
            if (ImGui::Selectable(types[i].label, g_spawn.type == i)) g_spawn.type = i;
        ImGui::EndCombo();
    }
    ImGui::SliderInt("How many", &g_spawn.count, 1, 10);
    const char* placements[] = {"In front of me", "Where I'm looking", "Behind me", "Somewhere around me"};
    int placement = static_cast<int>(g_spawn.placement);
    if (ImGui::Combo("Where", &placement, placements, 4)) g_spawn.placement = static_cast<enemies::Placement>(placement);
    ImGui::SliderFloat("Distance", &g_spawn.distance, 150.f, 3000.f, "%.0f");
    if (ImGui::BeginCombo("Weapon", enemies::WeaponName(g_spawn.weapon))) {
        for (int i = 0; i < enemies::WeaponCount(); ++i)
            if (ImGui::Selectable(enemies::WeaponName(i), g_spawn.weapon == i)) g_spawn.weapon = i;
        ImGui::EndCombo();
    }
    Toggle("Attacks the player", &g_spawn.attack);
    Toggle("Exact copy of an enemy in this level", &g_spawn.clone,
           "Copies a living enemy of that type (same look, voice and behaviour). Needs one in the level.");
    ImGui::BeginDisabled(!snap.inGame);
    if (ImGui::Button("Spawn", ImVec2(Em(8), 0))) {
        enemies::SpawnRequest r = g_spawn;
        Enqueue([r] { enemies::Spawn(r); });
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Remove spawned enemies")) Enqueue([] { enemies::KillAll(true); });
    ImGui::SameLine();
    if (DangerButton("Remove ALL enemies")) Enqueue([] { enemies::KillAll(false); });
    Hint("Enemies can only use animations, voices and AI that the current level has loaded. Spawning a character "
         "in a level where it never appears works best with a matching enemy already nearby; otherwise it may "
         "stand still or look wrong. Chris Walker, patients and the Walrider work in most levels.");

    Heading("Horde mode");
    Toggle("Horde mode", &s.hordeMode, "Keeps bringing extra enemies in over time, like the Ultimate Bendy mod.");
    ImGui::BeginDisabled(!s.hordeMode);
    SliderF("Every (seconds)", &s.hordeInterval, 5.f, 300.f, "%.0f", 45.f);
    SliderI("Max extra enemies", &s.hordeMaxEnemies, 1, 20, 6);
    std::string hordeLabel = s.hordeEnemyType < 0 || s.hordeEnemyType >= static_cast<int>(types.size())
                                 ? std::string("Random")
                                 : std::string(types[s.hordeEnemyType].label);
    if (ImGui::BeginCombo("Horde enemy", hordeLabel.c_str())) {
        if (ImGui::Selectable("Random", s.hordeEnemyType < 0)) s.hordeEnemyType = -1;
        for (int i = 0; i < static_cast<int>(types.size()); ++i)
            if (ImGui::Selectable(types[i].label, s.hordeEnemyType == i)) s.hordeEnemyType = i;
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();

    Heading(str::Format("Enemies in this level (%d, %d spawned by the mod)", static_cast<int>(snap.enemies.size()),
                        snap.modSpawnedEnemies)
                .c_str());
    if (snap.enemies.empty()) {
        Hint("No enemies loaded right now.");
        return;
    }
    ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp |
                         ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("##enemies", 5, tf, ImVec2(0, Em(14)))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Enemy", ImGuiTableColumnFlags_WidthStretch, 2.f);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthStretch, 1.f);
        ImGui::TableSetupColumn("Distance", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableSetupColumn("Model", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthStretch, 3.2f);
        ImGui::TableHeadersRow();
        for (const EnemyInfo& e : snap.enemies) {
            ImGui::PushID(e.index);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s%s", e.displayName.c_str(), e.spawnedByMod ? " (spawned)" : "");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(e.frozen ? "Frozen" : (e.state.empty() ? "-" : e.state.c_str()));
            ImGui::TableNextColumn();
            ImGui::Text("%.0f m", e.distance * 0.02f);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(e.mesh.c_str());
            ImGui::TableNextColumn();
            int32_t id = e.index;
            if (ImGui::SmallButton(e.frozen ? "Unfreeze" : "Freeze")) {
                bool f = !e.frozen;
                Enqueue([id, f] { enemies::SetFrozen(id, f); });
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(e.passive ? "Hostile" : "Ignore me")) {
                bool ignore = !e.passive;
                Enqueue([id, ignore] { enemies::SetIgnorePlayer(id, ignore); });
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Go to")) Enqueue([id] { enemies::TeleportTo(id); });
            ImGui::SameLine();
            if (ImGui::SmallButton("Bring")) Enqueue([id] { enemies::BringToPlayer(id); });
            ImGui::SameLine();
            if (ImGui::SmallButton("Model...")) {
                g_meshTargetEnemy = id;
                enemies::RequestMeshList();
                ImGui::OpenPopup("##meshpick");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) Enqueue([id] { enemies::Kill(id); });
            ImGui::SameLine();
            ImGui::SetNextItemWidth(Em(7));
            if (ImGui::BeginCombo("##weapon", "Weapon")) {
                for (int w = 0; w < enemies::WeaponCount(); ++w)
                    if (ImGui::Selectable(enemies::WeaponName(w))) Enqueue([id, w] { enemies::SetWeapon(id, w); });
                ImGui::EndCombo();
            }
            if (ImGui::BeginPopup("##meshpick")) {
                ImGui::SetNextItemWidth(Em(16));
                ImGui::InputTextWithHint("##filter", "filter (e.g. Soldier, Groom, Patient)", g_meshFilter,
                                         sizeof(g_meshFilter));
                std::vector<enemies::MeshEntry> meshes = enemies::MeshList();
                ImGui::BeginChild("##meshes", ImVec2(Em(24), Em(14)));
                for (const auto& m : meshes) {
                    if (g_meshFilter[0] && !str::IContains(m.path, g_meshFilter)) continue;
                    if (ImGui::Selectable(m.name.c_str())) {
                        std::string path = m.path;
                        int32_t target = g_meshTargetEnemy;
                        Enqueue([target, path] { enemies::SwapMesh(target, path); });
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", m.path.c_str());
                }
                if (meshes.empty()) ImGui::TextDisabled("Loading the list of models...");
                ImGui::EndChild();
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

}  // namespace omm::ui
