// Tests for the parts of the mod that write to the user's disk: INI tweaks
// (apply / revert / overlap / re-apply / backups) and the content-mod
// installer (copy, back up, uninstall). Everything happens in a temp folder.
#include "../src/core/fileutil.h"
#include "../src/core/ini.h"
#include "../src/core/paths.h"
#include "../src/core/settings.h"
#include "../src/game/initweaks.h"
#include "../src/game/modloader.h"
#include "test_util.h"

#include <cstdlib>
#include <string>

#if !defined(_WIN32)
#include <unistd.h>
#endif

using namespace omm;
namespace ini = omm::game::ini;
namespace mods = omm::game::mods;

namespace {
std::string MakeTempDir() {
#if defined(_WIN32)
    char buf[MAX_PATH];
    GetTempPathA(MAX_PATH, buf);
    std::string dir = std::string(buf) + "omm_test_" + std::to_string(GetCurrentProcessId());
    fs::CreateDirectories(dir);
    return dir;
#else
    char tmpl[] = "/tmp/omm_test_XXXXXX";
    char* d = mkdtemp(tmpl);
    return d ? std::string(d) : std::string("/tmp/omm_test_fallback");
#endif
}

void Write(const std::string& path, const std::string& text) {
    fs::CreateDirectories(fs::Parent(path));
    fs::WriteAllAtomic(path, text);
}

std::string Read(const std::string& path) {
    std::string s;
    fs::ReadAll(path, s);
    return s;
}

const char* kSystemSettings =
    "[SystemSettings]\r\n"
    "DynamicShadows=True\r\n"
    "MotionBlur=True\r\n"
    "DepthOfField=True\r\n"
    "AmbientOcclusion=True\r\n"
    "bAllowLightShafts=True\r\n"
    "LensFlares=True\r\n"
    "MaxShadowResolution=1120\r\n"
    "MaxWholeSceneDominantShadowResolution=1344\r\n"
    "\r\n"
    "[SystemSettingsBucket1]\r\n"
    "MotionBlur=False\r\n";

const char* kEngine =
    "[Engine.Engine]\r\n"
    "bSmoothFrameRate=TRUE\r\n"
    "MaxSmoothedFrameRate=62\r\n"
    "\r\n"
    "[FullScreenMovie]\r\n"
    "bForceNoMovies=FALSE\r\n"
    "StartupMovies=IntroLogo\r\n"
    "SkippableMovies=IntroLogo\r\n";

const char* kGame =
    "[OLGame.OLHero]\r\n"
    "FirstFingerlessCheckpoint=Male_TortureDone\r\n"
    "NrmBatteryDuration=150.0\r\n";
}  // namespace

void TestIniTweaks() {
    std::printf("[ini tweaks]\n");
    std::string root = MakeTempDir();
    std::string cfg = fs::Join(root, "Config");
    std::string game = fs::Join(root, "Outlast");
    std::string mod = fs::Join(fs::Join(fs::Join(game, "Binaries"), "Win64"), "OutlastModMenu");
    paths::SetForTests(mod, game, cfg);
    Write(fs::Join(cfg, "OLSystemSettings.ini"), kSystemSettings);
    Write(fs::Join(cfg, "OLEngine.ini"), kEngine);
    Write(fs::Join(cfg, "OLGame.ini"), kGame);
    Write(fs::Join(cfg, "OLInput.ini"), "[Engine.PlayerInput]\r\nbEnableMouseSmoothing=true\r\n");
    Write(fs::Join(cfg, "OLEnemy.ini"), "[OLGame.OLEnemyPawn]\r\nbInvestigateLockers=true\r\n");
    Settings::Get().Load(fs::Join(mod, "config.ini"));

    const ini::Tweak* postfx = ini::FindTweak("perf_postfx");
    const ini::Tweak* cheap = ini::FindTweak("perf_shadows_cheap");
    const ini::Tweak* hiShadows = ini::FindTweak("qual_shadows");
    const ini::Tweak* intro = ini::FindTweak("vis_skip_intro");
    const ini::Tweak* fingers = ini::FindTweak("story_fingerless");
    CHECK(postfx && cheap && hiShadows && intro && fingers);
    if (!postfx || !cheap || !hiShadows || !intro || !fingers) return;

    std::string ss = fs::Join(cfg, "OLSystemSettings.ini");
    std::string original = Read(ss);
    std::string err;
    CHECK(ini::StateOf(*postfx) == ini::TweakState::Off);
    CHECK(ini::Apply(*postfx, err));
    CHECK(ini::StateOf(*postfx) == ini::TweakState::On);
    CHECK(ini::Read(ini::File::SystemSettings, "SystemSettings", "MotionBlur") == "False");
    // Other sections with the same key are untouched.
    CHECK(ini::Read(ini::File::SystemSettings, "SystemSettingsBucket1", "MotionBlur") == "False");
    CHECK(fs::IsDirectory(fs::Join(fs::Join(mod, "ini_backup"), "original")));
    CHECK(Read(fs::Join(fs::Join(fs::Join(mod, "ini_backup"), "original"), "OLSystemSettings.ini")) == original);
    CHECK(ini::Revert(*postfx, err));
    CHECK(Read(ss) == original);  // byte-for-byte, CRLF included
    CHECK(ini::StateOf(*postfx) == ini::TweakState::Off);

    // Two tweaks setting the same key: reverting one keeps the other's value.
    CHECK(ini::Apply(*cheap, err));
    CHECK(ini::Apply(*hiShadows, err));
    CHECK(ini::Read(ini::File::SystemSettings, "SystemSettings", "MaxShadowResolution") == "2048");
    CHECK(ini::Revert(*hiShadows, err));
    CHECK(ini::Read(ini::File::SystemSettings, "SystemSettings", "MaxShadowResolution") == "512");
    CHECK(ini::Revert(*cheap, err));
    CHECK(ini::Read(ini::File::SystemSettings, "SystemSettings", "MaxShadowResolution") == "1120");
    CHECK(Read(ss) == original);

    // Removing a key and putting it back.
    std::string eng = fs::Join(cfg, "OLEngine.ini");
    std::string engOriginal = Read(eng);
    CHECK(ini::Apply(*intro, err));
    CHECK(Read(eng).find("StartupMovies") == std::string::npos);
    CHECK(Read(eng).find("SkippableMovies=IntroLogo") != std::string::npos);
    CHECK(ini::Revert(*intro, err));
    CHECK(ini::Read(ini::File::Engine, "FullScreenMovie", "StartupMovies") == "IntroLogo");

    // The game overwrote a tweak: ReapplyActive puts it back.
    CHECK(ini::Apply(*fingers, err));
    std::string gameIni = fs::Join(cfg, "OLGame.ini");
    Write(gameIni, kGame);
    CHECK(ini::StateOf(*fingers) == ini::TweakState::Off);
    CHECK(ini::ReapplyActive() == 1);
    CHECK(ini::Read(ini::File::Game, "OLGame.OLHero", "FirstFingerlessCheckpoint") == "Admin_Gates");
    CHECK(ini::ReapplyActive() == 0);
    CHECK(ini::Revert(*fingers, err));
    CHECK(ini::Read(ini::File::Game, "OLGame.OLHero", "FirstFingerlessCheckpoint") == "Male_TortureDone");

    // Backups and restore.
    std::string dir = ini::BackupNow(err);
    CHECK(!dir.empty());
    CHECK(ini::Apply(*postfx, err));
    std::vector<std::string> backups = ini::Backups();
    CHECK(!backups.empty() && backups.front() == "original");
    CHECK(ini::RestoreBackup("original", err));
    CHECK(Read(ss) == original);
    CHECK(ini::StateOf(*postfx) == ini::TweakState::Off);
    CHECK(!ini::RestoreBackup("../../etc", err));
}

void TestModInstaller() {
    std::printf("[mod installer]\n");
    std::string root = MakeTempDir();
    std::string game = fs::Join(root, "Outlast");
    std::string mod = fs::Join(fs::Join(fs::Join(game, "Binaries"), "Win64"), "OutlastModMenu");
    paths::SetForTests(mod, game, fs::Join(root, "Config"));
    std::string cooked = paths::CookedDir();
    Write(fs::Join(cooked, "Existing.upk"), "original game file");

    std::string src = fs::Join(fs::Join(mod, "mods"), "MyMap");
    Write(fs::Join(src, "mod.ini"), "[Mod]\r\nName=My Map\r\nAuthor=Someone\r\nStartMap=MyMap_P\r\n");
    Write(fs::Join(fs::Join(src, "CookedPCConsole"), "MyMap_P.udk"), "map data");
    Write(fs::Join(fs::Join(fs::Join(src, "Game"), fs::Join("OLGame", "CookedPCConsole")), "Existing.upk"), "modded file");

    std::vector<mods::ContentMod> list = mods::ScanMods();
    CHECK(list.size() == 1);
    if (list.size() != 1) return;
    CHECK(list[0].name == "My Map" && list[0].startMap == "MyMap_P" && list[0].fileCount == 2 && !list[0].installed);

    std::string err;
    CHECK(mods::Install(list[0], err));
    std::string installedMap = fs::Join(fs::Join(fs::Join(cooked, "OMM_Mods"), "MyMap"), "MyMap_P.udk");
    CHECK(Read(installedMap) == "map data");
    CHECK(Read(fs::Join(cooked, "Existing.upk")) == "modded file");
    CHECK(mods::ScanMods()[0].installed);

    std::vector<mods::MapFile> maps = mods::ListMaps();
    CHECK(!maps.empty() && maps[0].name == "MyMap_P" && maps[0].fromMod);

    CHECK(mods::Uninstall(mods::ScanMods()[0], err));
    CHECK(!fs::Exists(installedMap));
    CHECK(Read(fs::Join(cooked, "Existing.upk")) == "original game file");
    CHECK(!mods::ScanMods()[0].installed);

    // A mod may not write outside the game folder.
    std::string evil = fs::Join(fs::Join(mod, "mods"), "Evil");
    Write(fs::Join(fs::Join(fs::Join(evil, "Game"), ".."), "outside.txt"), "x");  // lands in mods/Evil/outside.txt
    mods::ContentMod e;
    e.folder = "../Evil";
    CHECK(!mods::Install(e, err));
}
