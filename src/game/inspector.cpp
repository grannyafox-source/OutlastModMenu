#include "inspector.h"

#include "outlast.h"
#include "state.h"

#include "../core/strutil.h"
#include "../core/sync.h"

namespace omm::game::inspector {

using namespace ue3;

namespace {
Mutex g_lock;
Result g_last;

void Publish(Result&& r) {
    LockGuard lock(g_lock);
    g_last = std::move(r);
}

UObject* Resolve(const std::string& name, const std::string& cls) {
    const World& w = W();
    if (str::IEquals(name, "Hero") || str::IEquals(name, "Player")) return w.hero;
    if (str::IEquals(name, "PC") || str::IEquals(name, "PlayerController")) return w.pc;
    if (str::IEquals(name, "WorldInfo")) return w.worldInfo;
    if (str::IEquals(name, "Game")) return w.game;
    if (str::IEquals(name, "HUD")) return w.hud;
    if (str::IEquals(name, "CheatManager")) return w.cheat;
    if (str::IEquals(name, "Engine")) return w.engine;
    if (str::IEquals(name, "LocalPlayer")) return w.localPlayer;
    if (name.find('.') != std::string::npos) return FindObjectByPath(name, cls.empty() ? nullptr : cls.c_str());
    return FindObject(name.c_str(), cls.empty() ? nullptr : cls.c_str());
}
}  // namespace

void InspectObject(const std::string& name, const std::string& className) {
    Enqueue([name, className] {
        Result r;
        UObject* o = Resolve(name, className);
        if (!o) {
            r.title = "Not found: " + name;
            Publish(std::move(r));
            return;
        }
        r.title = FullName(o);
        for (UField* f : Fields(ClassOf(o), true)) {
            if (!IsProperty(f)) continue;
            PropRef ref = Prop(o, Name(f).c_str());
            r.lines.push_back(str::Format("%-28s %-16s %s", Name(f).c_str(), PropType(f).c_str(),
                                          ref ? ValueToString(ref).c_str() : "?"));
            if (r.lines.size() >= 1500) break;
        }
        Publish(std::move(r));
    });
}

void ListInstances(const std::string& className) {
    Enqueue([className] {
        Result r;
        UClass* cls = FindClass(className.c_str());
        if (!cls) {
            r.title = "Class not found: " + className;
            Publish(std::move(r));
            return;
        }
        std::vector<UObject*> list = FindInstances(cls, 500);
        r.title = str::Format("%zu instance(s) of %s", list.size(), className.c_str());
        for (UObject* o : list) r.lines.push_back(PathName(o));
        Publish(std::move(r));
    });
}

void ListFunctions(const std::string& className) {
    Enqueue([className] {
        Result r;
        UClass* cls = FindClass(className.c_str());
        if (!cls) {
            r.title = "Class not found: " + className;
            Publish(std::move(r));
            return;
        }
        r.title = "Functions of " + className;
        for (UField* f : Fields(cls, true)) {
            if (!IsFunction(f)) continue;
            uint32_t flags = FuncFlags(f);
            std::string params;
            for (UField* p : Fields(f, false)) {
                if (!IsProperty(p)) continue;
                if (!params.empty()) params += ", ";
                params += PropType(p) + " " + Name(p);
            }
            r.lines.push_back(str::Format("%s%s%s(%s)", (flags & FUNC_Exec) ? "exec " : "",
                                          (flags & FUNC_Native) ? "native " : "", Name(f).c_str(), params.c_str()));
            if (r.lines.size() >= 1500) break;
        }
        Publish(std::move(r));
    });
}

Result Last() {
    LockGuard lock(g_lock);
    return g_last;
}

}  // namespace omm::game::inspector
