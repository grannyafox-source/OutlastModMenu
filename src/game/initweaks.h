// Edits of the game's own config files: the OL*.ini files in
// "Documents\My Games\Outlast\OLGame\Config". The engine reads them at
// start-up, so changes apply the next time the game starts. Every original
// value is remembered so each tweak can be reverted, and the files are
// backed up before the first edit.
#pragma once

#include <string>
#include <vector>

namespace omm::game::ini {

enum class File { Engine = 0, Game, Input, SystemSettings, Enemy, Count };
const char* FileName(File f);
std::string FilePath(File f);

struct Change {
    File file;
    const char* section;
    const char* key;
    const char* value;
};

struct Tweak {
    const char* id;
    const char* category;
    const char* title;
    const char* description;
    std::vector<Change> changes;
};
const std::vector<Tweak>& Tweaks();
const Tweak* FindTweak(const std::string& id);

enum class TweakState { Off, On, Partial };
TweakState StateOf(const Tweak& t);
bool Apply(const Tweak& t, std::string& error);
bool Revert(const Tweak& t, std::string& error);

struct Preset {
    const char* name;
    const char* description;
    std::vector<const char*> tweaks;
};
const std::vector<Preset>& Presets();
int ApplyPreset(const Preset& p, std::string& error);  // number of tweaks applied

// Backups of all OL*.ini files: OutlastModMenu\ini_backup\<date-time>.
std::string BackupNow(std::string& error);
std::vector<std::string> Backups();  // newest first, folder names
bool RestoreBackup(const std::string& folder, std::string& error);
void EnsureInitialBackup();

// Startup: puts back tweaks that the game overwrote when it last saved its
// settings. Returns how many values had to be re-applied.
int ReapplyActive();

// Direct access for the advanced editor.
std::string Read(File f, const std::string& section, const std::string& key);
bool Write(File f, const std::string& section, const std::string& key, const std::string& value, std::string& error);

}  // namespace omm::game::ini
