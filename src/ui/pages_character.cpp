// Character page: outfits, injuries, walking style, size and model swaps.
#include "pages.h"

#include "../core/paths.h"
#include "../core/strutil.h"
#include "../game/actions.h"
#include "../game/enemies.h"
#include "../game/modloader.h"

namespace omm::ui {

using namespace game;

namespace {
struct Target {
    const char* id;
    const char* label;
};
const Target kTargets[] = {
    {"AllEnemies", "Every enemy"},
    {"Hero", "The player (Miles / Waylon)"},
    {"OLEnemySoldier", "Chris Walker (and the Groom unless set separately)"},
    {"OLEnemyGroom", "Eddie Gluskin (the Groom)"},
    {"OLEnemySurgeon", "Dr. Trager"},
    {"OLEnemyCannibal", "Frank Manera (the Cannibal)"},
    {"OLEnemyGenericPatient", "Patients"},
    {"OLEnemyNanoCloud", "The Walrider"},
};
int g_target = 2;
char g_meshPath[256] = "";
char g_filter[64] = "";
float g_heroScale = 1.f;
std::vector<enemies::ModelPack> g_packs;
bool g_packsScanned = false;

const char* TargetLabel(const std::string& id) {
    for (const Target& t : kTargets)
        if (id == t.id) return t.label;
    return id.c_str();
}
}  // namespace

void PageCharacter(Ctx& c) {
    ModState& s = c.s;
    const Snapshot& snap = c.snap;

    Heading("Outfit");
    ImGui::BeginDisabled(!snap.inGame);
    const actions::Outfit outfits[] = {actions::Outfit::Original, actions::Outfit::Fingerless, actions::Outfit::ITTech,
                                       actions::Outfit::Prisoner};
    for (actions::Outfit o : outfits) {
        if (ImGui::Button(actions::OutfitName(o))) Enqueue([o] { actions::SetOutfit(o); });
    }
    ImGui::EndDisabled();
    if (!snap.heroMesh.empty()) ImGui::TextDisabled("Current model: %s", snap.heroMesh.c_str());
    Hint("'Missing fingers' gives Miles the hands he has after Dr. Trager's operating room - at any point of the "
         "game. To start every new game that way, use INI Tweaks > Character > 'Missing fingers from the start'. "
         "Outfits only work where the game has them loaded (the Whistleblower outfits are usually only in the DLC).");

    Heading("Injuries and walking style");
    const auto& presets = actions::CharacterPresets();
    for (int i = 0; i < static_cast<int>(presets.size()); ++i) {
        if (i) ImGui::SameLine();
        if (ImGui::Button(presets[i].name)) actions::ApplyCharacterPreset(i);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", presets[i].description);
    }
    Toggle("Limp", &s.limp, "Miles' injured limp (used late in the game, before the Walrider).");
    Toggle("Hobble", &s.hobble, "Heavier, uneven steps.");
    if (s.hobble) SliderF("Hobble strength", &s.hobbleIntensity, 0.f, 1.f, "%.2f", 0.5f);
    Toggle("Force walking animation set", &s.walkingStyleOverride);
    if (s.walkingStyleOverride) {
        const char* styles[] = {"Default", "Wading through water", "Limping", "Hobbling", "Prototype A", "Prototype B",
                                "Prototype C", "Prototype D", "Prototype E"};
        ImGui::Combo("Walking style", &s.walkingStyle, styles, IM_ARRAYSIZE(styles));
        Help("The 'Prototype' styles are unused animation sets left in the game; some may look odd.");
    }
    Toggle("Cracked camcorder lens", &s.cameraCracked);

    Heading("Size");
    ImGui::SliderFloat("Player size", &g_heroScale, 0.25f, 3.f, "%.2fx");
    ImGui::SameLine();
    if (ImGui::Button("Apply size")) {
        float v = g_heroScale;
        Enqueue([v] { actions::SetHeroScale(v); });
    }
    Hint("Changes how big the player model is drawn (the camera and collision stay the same).");

    Heading("Model swapper");
    Hint("Swap any character's 3D model for another one. Outlast can show any model that is loaded: other "
         "characters, patients, Father Martin... Custom models (for example a Thomas the Tank Engine for Chris "
         "Walker, or Quagmire for Eddie Gluskin) come from fan-made model packs - see 'Model packs' below.");
    Toggle("Apply swaps automatically", &s.autoApplyModelSwaps, "Keeps your swaps on enemies that spawn later.");

    if (ImGui::BeginCombo("Who", kTargets[g_target].label)) {
        for (int i = 0; i < IM_ARRAYSIZE(kTargets); ++i)
            if (ImGui::Selectable(kTargets[i].label, g_target == i)) g_target = i;
        ImGui::EndCombo();
    }
    ImGui::InputTextWithHint("Model", "pick below or type Package.Group.Mesh", g_meshPath, sizeof(g_meshPath));
    if (ImGui::Button("Swap")) {
        if (g_meshPath[0]) enemies::SetModelRule(kTargets[g_target].id, str::Trim(g_meshPath));
    }
    ImGui::SameLine();
    if (ImGui::Button("Undo for this character")) enemies::RemoveModelRule(kTargets[g_target].id);
    ImGui::SameLine();
    if (ImGui::Button("Shuffle everyone's models")) Enqueue([] { enemies::RandomizeAllModels(); });
    ImGui::SameLine();
    if (DangerButton("Undo all swaps")) enemies::ClearModelRules();

    if (ImGui::TreeNode("Loaded models")) {
        if (ImGui::Button("Refresh list")) enemies::RequestMeshList();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(Em(14));
        ImGui::InputTextWithHint("##flt", "filter", g_filter, sizeof(g_filter));
        std::vector<enemies::MeshEntry> meshes = enemies::MeshList();
        if (meshes.empty()) {
            ImGui::TextDisabled("Press 'Refresh list' while in a level.");
        } else {
            ImGui::BeginChild("##meshlist", ImVec2(0, Em(12)), ImGuiChildFlags_Borders);
            for (const auto& m : meshes) {
                if (g_filter[0] && !str::IContains(m.path, g_filter)) continue;
                if (ImGui::Selectable(m.name.c_str(), std::string(g_meshPath) == m.path)) {
                    std::snprintf(g_meshPath, sizeof(g_meshPath), "%s", m.path.c_str());
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", m.path.c_str());
            }
            ImGui::EndChild();
        }
        ImGui::TreePop();
    }

    enemies::ModelRules rules = enemies::GetModelRules();
    if (!rules.empty()) {
        ImGui::TextUnformatted("Active swaps:");
        for (const auto& kv : rules) ImGui::BulletText("%s  ->  %s", TargetLabel(kv.first), kv.second.c_str());
    }

    Heading("Model packs");
    if (!g_packsScanned) {
        g_packs = enemies::ScanModelPacks();
        g_packsScanned = true;
    }
    if (ImGui::Button("Rescan")) g_packs = enemies::ScanModelPacks();
    ImGui::SameLine();
    if (ImGui::Button("Open models folder")) mods::OpenFolder(paths::ModSubdir("models"));
    if (g_packs.empty()) {
        Hint("No model packs yet. A model pack is a small .ini file in OutlastModMenu\\models that lists which "
             "model replaces which character; the model itself is a cooked Unreal Engine 3 package (.upk) "
             "installed with the Mod Loader. See docs\\MODDING.md for the format and an example.");
    }
    for (size_t i = 0; i < g_packs.size(); ++i) {
        const enemies::ModelPack& p = g_packs[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::BulletText("%s%s%s", p.name.c_str(), p.author.empty() ? "" : "  by ", p.author.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Activate")) {
            enemies::ModelPack copy = p;
            Enqueue([copy] { enemies::ActivatePack(copy); });
        }
        if (!p.description.empty()) {
            ImGui::Indent();
            Hint(p.description.c_str());
            ImGui::Unindent();
        }
        for (const auto& sw : p.swaps) ImGui::TextDisabled("    %s -> %s", TargetLabel(sw.target), sw.mesh.c_str());
        ImGui::PopID();
    }
}

}  // namespace omm::ui
