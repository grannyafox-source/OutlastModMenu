// Host-side unit tests (Linux/macOS/Windows console). Build with
// tests/run_tests.sh. They cover the pieces that can be verified without the
// game: engine layout discovery, INI editing and string helpers.
#include "../src/core/fileutil.h"
#include "../src/core/ini.h"
#include "../src/core/strutil.h"
#include "../src/ue3/engine.h"
#include "../src/ue3/scanner.h"
#include "fake_ue3.h"

#include <cstdio>
#include <string>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                             \
    do {                                                                        \
        ++g_checks;                                                             \
        if (!(cond)) {                                                          \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

#define CHECK_EQ(a, b)                                                                                  \
    do {                                                                                                \
        ++g_checks;                                                                                     \
        long long va_ = static_cast<long long>(a), vb_ = static_cast<long long>(b);                     \
        if (va_ != vb_) {                                                                               \
            std::printf("  FAIL %s:%d: %s == %s (0x%llX vs 0x%llX)\n", __FILE__, __LINE__, #a, #b,      \
                        static_cast<unsigned long long>(va_), static_cast<unsigned long long>(vb_));    \
            ++g_failures;                                                                               \
        }                                                                                               \
    } while (0)

using omm::ue3::Scanner;

static void TestScanner(const fake::Config& cfg, const char* label) {
    std::printf("[scanner] %s (pointer size %zu)\n", label, sizeof(void*));
    fake::World w(cfg);
    fake::Graph g = fake::Build(w);
    w.Publish(0x1800, 0x3400);
    Scanner s(w, {w.DataRange()},
              [](uintptr_t a) { return a >= fake::kCodeBase && a < fake::kCodeBase + 0x100000; });
    bool ok = s.RunAll();
    CHECK(ok);
    for (const std::string& n : s.Result().notes) std::printf("    note: %s\n", n.c_str());
    const omm::ue3::Layout& L = s.Result().layout;
    CHECK_EQ(s.Result().gnames, w.NamesAddr());
    CHECK_EQ(s.Result().gobjects, w.ObjectsAddr());
    CHECK_EQ(L.nameString, cfg.nameString);
    CHECK_EQ(L.nameIndexField, cfg.nameIndex);
    CHECK(L.nameIndexShifted);
    CHECK_EQ(L.objIndex, cfg.objIndex);
    CHECK_EQ(L.objOuter, cfg.objOuter);
    CHECK_EQ(L.objName, cfg.objName);
    CHECK_EQ(L.objClass, cfg.objClass);
    CHECK_EQ(L.fieldNext, cfg.objSize);
    CHECK_EQ(L.structSuper, cfg.structSuper());
    CHECK_EQ(L.structChildren, cfg.structChildren());
    CHECK_EQ(L.propArrayDim, cfg.propArrayDim());
    CHECK_EQ(L.propElementSize, cfg.propElementSize());
    CHECK_EQ(L.propFlags, cfg.propFlags());
    CHECK_EQ(L.propOffset, cfg.propOffset());
    CHECK_EQ(L.propSize, cfg.propSize());
    CHECK_EQ(L.classPropMeta, cfg.propSize() + sizeof(void*));
    CHECK_EQ(L.funcFunc, cfg.funcFunc());
    CHECK_EQ(L.funcNative, cfg.funcNative());
    CHECK_EQ(L.funcFlags, cfg.funcFlags());
    CHECK_EQ(L.funcParmsSize, cfg.funcParms());
    CHECK_EQ(L.funcRetOffset, cfg.funcRet());
    CHECK_EQ(s.Result().processInternal, fake::kProcessInternal);

    // Runtime API on top of the detected layout.
    namespace u = omm::ue3;
    CHECK(u::Init(s.Result()));
    CHECK(reinterpret_cast<uintptr_t>(u::FindClass("Actor")) == g.actor);
    CHECK(reinterpret_cast<uintptr_t>(u::FindClass("Pawn")) == g.pawn);
    CHECK(u::FindClass("DoesNotExist") == nullptr);
    CHECK(reinterpret_cast<uintptr_t>(u::ClassClass()) == g.classClass);
    auto* actorObj = reinterpret_cast<u::UObject*>(g.actorInstance);
    auto* pawnObj = reinterpret_cast<u::UObject*>(g.pawnInstance);
    CHECK(u::IsValid(actorObj));
    CHECK(!u::IsValid(reinterpret_cast<u::UObject*>(g.actorInstance + 8)));
    CHECK(u::Name(actorObj) == "Actor_7");
    CHECK(u::FullName(actorObj) == "Actor Engine.Actor_7");
    CHECK(u::IsA(pawnObj, u::FindClass("Actor")));
    CHECK(u::IsA(pawnObj, "Actor"));
    CHECK(!u::IsA(actorObj, u::FindClass("Pawn")));
    u::FVector v;
    CHECK(u::GetVector(actorObj, "Location", v) && v.X == 1.5f && v.Y == -2.0f && v.Z == 300.0f);
    CHECK(u::SetVector(actorObj, "Location", u::FVector(10, 20, 30)));
    CHECK(u::GetVector(actorObj, "Location", v) && v.Z == 30.0f);
    float fl = 0;
    CHECK(!u::GetFloat(actorObj, "Location", fl));  // wrong type is rejected
    bool b = true;
    CHECK(u::GetBool(actorObj, "bHidden", b) && !b);
    CHECK(u::GetBool(actorObj, "bStatic", b) && b);
    CHECK(u::SetBool(actorObj, "bHidden", true));
    CHECK(u::GetBool(actorObj, "bHidden", b) && b);
    CHECK(u::GetBool(actorObj, "bStatic", b) && b);  // neighbouring bit untouched
    CHECK(u::Obj(actorObj, "Owner") == pawnObj);
    CHECK(u::Int(pawnObj, "Health") == 100);
    CHECK(u::SetFloat(pawnObj, "GroundSpeed", 450.0f) && u::Float(pawnObj, "GroundSpeed") == 450.0f);
    CHECK(u::SetBool(pawnObj, "Modifiers.bShouldAttack", true));
    CHECK(u::Bool(pawnObj, "Modifiers.bShouldAttack") && !u::Bool(pawnObj, "Modifiers.bUseKillingBlow"));
    CHECK(u::SetByte(pawnObj, "Modifiers.WeaponToUse", 8));
    uint8_t wpn = 0;
    CHECK(u::GetByte(pawnObj, "Modifiers.WeaponToUse", wpn) && wpn == 8);
    CHECK(!u::Prop(pawnObj, "Modifiers.Missing"));
    CHECK(u::FindFunction(u::FindClass("Actor"), "SetLocation") == reinterpret_cast<u::UObject*>(g.setLocation));
    CHECK(u::FindFunction(u::FindClass("Pawn"), "SetLocation") == reinterpret_cast<u::UObject*>(g.setLocation));
    CHECK(u::FindProperty(u::FindClass("Actor"), "SetLocation") == nullptr);  // functions are not properties
    CHECK(u::FuncNative(reinterpret_cast<u::UObject*>(g.setLocation)) == 267);
    CHECK(u::FindInstances(u::FindClass("Pawn")).size() == 1);  // the Default__ object is skipped
    u::FName fn;
    CHECK(u::FindName("ScriptFunc12", fn) && u::NameString(fn.Index) == "ScriptFunc12" && fn.Number == 0);
    CHECK(u::FindName("actor", fn) && u::NameString(fn.Index) == "Actor");
    // Like the engine's FName constructor, "Actor_7" means name "Actor" with Number 7 + 1.
    CHECK(u::FindName("Actor_7", fn) && u::NameString(fn.Index) == "Actor" && fn.Number == 8);
    CHECK(u::NameToString(fn) == "Actor_7");
    CHECK(u::FindName("Male_ward_01", fn) == false);  // leading zero: not split, and not in the table
    CHECK(!u::FindName("NoSuchName_123", fn));
}

static void TestScannerRejectsGarbage() {
    std::printf("[scanner] empty data section\n");
    fake::Config cfg;
    fake::World w(cfg);
    Scanner s(w, {w.DataRange()}, [](uintptr_t) { return false; });
    CHECK(!s.RunAll());
}

static void TestIni() {
    std::printf("[ini] edit preserves formatting\n");
    const std::string text =
        "[OLGame.OLHero]\r\n"
        "NormalWalkSpeed=200\r\n"
        "NormalRunSpeed=450\r\n"
        "ElectricEffectMode=2 ; 0: sine\r\n"
        "\r\n"
        "[DebugLookZone MobileInputZone]\r\n"
        "InputKey=MouseY\r\n"
        "\r\n"
        "[OLGame.OLEnemyCannibal]\r\n"
        "HardElectricitySpeedValues=(PatrolSpeed=120,InvestigateSpeed=120,ChaseSpeed=325)\r\n"
        "HardElectricitySpeedValues=(PatrolSpeed=120,InvestigateSpeed=120,ChaseSpeed=325)\r\n"
        "\r\n"
        "[OLGame.OLPlayerInput]\r\n"
        "Bindings=(Name=\"F\",Command=\"OLA_ToggleNightVision\")\r\n";
    omm::IniDocument doc;
    doc.Parse(text);
    CHECK(doc.ToString() == text);
    CHECK(doc.Get("olgame.olhero", "normalwalkspeed").value_or("") == "200");
    CHECK(doc.Get("DebugLookZone MobileInputZone", "InputKey").value_or("") == "MouseY");
    CHECK(!doc.Get("OLGame.OLHero", "Missing").has_value());

    CHECK(doc.Set("OLGame.OLHero", "NormalRunSpeed", "900"));
    CHECK(!doc.Set("OLGame.OLHero", "NormalRunSpeed", "900"));
    CHECK(doc.Get("OLGame.OLHero", "NormalRunSpeed").value_or("") == "900");
    CHECK(doc.Set("OLGame.OLHero", "JumpClearanceWalking", "400"));
    // The new key goes before the blank separator line of its section.
    std::string out = doc.ToString();
    CHECK(out.find("ElectricEffectMode=2 ; 0: sine\r\nJumpClearanceWalking=400\r\n\r\n[DebugLookZone") !=
          std::string::npos);

    // Duplicate keys are all updated.
    doc.Set("OLGame.OLEnemyCannibal", "HardElectricitySpeedValues", "(PatrolSpeed=1,InvestigateSpeed=1,ChaseSpeed=1)");
    auto all = doc.GetAll("OLGame.OLEnemyCannibal", "HardElectricitySpeedValues");
    CHECK(all.size() == 2);
    CHECK(all.size() == 2 && all[0] == all[1]);

    // Arrays.
    CHECK(doc.AddUnique("OLGame.OLPlayerInput", "Bindings", "(Name=\"F2\",Command=\"Ghost\")"));
    CHECK(!doc.AddUnique("OLGame.OLPlayerInput", "Bindings", "(Name=\"F2\",Command=\"Ghost\")"));
    CHECK(doc.GetAll("OLGame.OLPlayerInput", "Bindings").size() == 2);
    CHECK(doc.Remove("OLGame.OLPlayerInput", "Bindings", std::string("(Name=\"F2\",Command=\"Ghost\")")) == 1);

    // New section.
    CHECK(doc.Set("Patches", "StruggleNoFail", "True"));
    CHECK(doc.Get("Patches", "StruggleNoFail").value_or("") == "True");
    CHECK(doc.ToString().find("\r\n[Patches]\r\nStruggleNoFail=True\r\n") != std::string::npos);

    std::printf("[ini] UTF-16 round trip\n");
    const std::string path = "/tmp/omm_ini_test_utf16.ini";
    std::string bytes = "\xFF\xFE";
    for (char ch : std::string("[A]\r\nKey=Value\r\n")) {
        bytes.push_back(ch);
        bytes.push_back('\0');
    }
    CHECK(omm::fs::WriteAllAtomic(path, bytes));
    omm::IniDocument u;
    CHECK(u.Load(path));
    CHECK(u.GetEncoding() == omm::IniDocument::Encoding::Utf16LE);
    CHECK(u.Get("A", "Key").value_or("") == "Value");
    u.Set("A", "Key", "Other");
    CHECK(u.Save(path));
    std::string back;
    CHECK(omm::fs::ReadAll(path, back));
    CHECK(back.size() > 2 && static_cast<unsigned char>(back[0]) == 0xFF && static_cast<unsigned char>(back[1]) == 0xFE);
    omm::fs::DeleteFileAt(path);
}

static void TestStrings() {
    std::printf("[str] helpers\n");
    float f = 0;
    CHECK(omm::str::ParseFloat("2.0f", f) && f == 2.0f);
    CHECK(omm::str::ParseFloat(" 87.243 ", f) && f > 87.24f && f < 87.25f);
    CHECK(!omm::str::ParseFloat("abc", f));
    int i = 0;
    CHECK(omm::str::ParseInt("-12", i) && i == -12);
    bool b = false;
    CHECK(omm::str::ParseBool("TRUE", b) && b);
    CHECK(omm::str::ParseBool("off", b) && !b);
    CHECK(omm::str::FloatToIni(150.0f) == "150.0");
    CHECK(omm::str::FloatToIni(0.025f) == "0.025");
    std::string s = "Caf\xC3\xA9 \xF0\x9F\x98\x80";
    CHECK(omm::str::Utf16ToUtf8(omm::str::Utf8ToUtf16(s)) == s);
    CHECK(omm::str::Utf8ToUtf16("\xF0\x9F\x98\x80").size() == 2);
    CHECK(omm::fs::IsInside("C:\\Games\\Outlast", "C:\\Games\\Outlast\\OLGame\\x.upk"));
    CHECK(!omm::fs::IsInside("C:\\Games\\Outlast", "C:\\Games\\Outlast\\..\\Windows\\x.dll"));
    CHECK(!omm::fs::IsInside("C:\\Games\\Outlast", "C:\\Games\\OutlastOther\\x"));
}

int main() {
    TestStrings();
    TestIni();
    TestScannerRejectsGarbage();

    fake::Config standard;
    TestScanner(standard, "engine-header layout");

    // Perturbed layout: UObject members moved and a different FNameEntry, so
    // the scanner must fall back to searching instead of the preferred guess.
    fake::Config moved;
    moved.nameString = sizeof(void*) == 8 ? 0x10 : 0x0C;
    moved.nameIndex = 0;
    moved.objOuter = sizeof(void*) == 8 ? 0x48 : 0x30;  // FName (8 bytes) now precedes Outer
    moved.objName = sizeof(void*) == 8 ? 0x40 : 0x28;
    moved.objClass = sizeof(void*) == 8 ? 0x58 : 0x38;
    moved.objIndex = sizeof(void*) == 8 ? 0x34 : 0x1C;
    moved.objSize = sizeof(void*) == 8 ? 0x68 : 0x40;
    TestScanner(moved, "perturbed layout");

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
