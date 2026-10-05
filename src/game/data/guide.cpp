#include "guide.h"

namespace omm::data {

// Numbers quoted below come from the game's own configuration (OLGame.ini and
// OLEnemy.ini, Normal difficulty unless stated) and its UI text.
const std::vector<GuideSection>& Guide() {
    static const std::vector<GuideSection> guide = {
        {"Start here",
         {{"Opening the menu",
           "- Press INSERT or F1 at any time (INSERT can be changed in Settings).\n"
           "- Or open the game's own Options / pause screen: a MOD MENU button appears there.\n"
           "- Mouse works, and so does the keyboard: arrow keys move, Space/Enter activate, Esc goes back.\n"
           "- While the menu is open the game does not receive your mouse and keyboard."},
          {"What gets changed",
           "Toggles in this menu change the running game only. Nothing is written to your save files.\n"
           "- Turning a toggle off restores the value the game had before.\n"
           "- The INI Tweaks tab is the only place that edits Outlast's config files, and it always makes a backup "
           "first (OutlastModMenu\\ini_backup).\n"
           "- 'Load checkpoint' starts the game at that checkpoint without writing a save."},
          {"Developer cheats",
           "Noclip (Ghost), free camera, unlimited batteries, game speed and checkpoint loading use Outlast's own "
           "developer cheat manager (OLCheatManager). Retail copies ship with it switched off; the mod switches it "
           "on for you when you use one of these features."},
          {"If something does not work",
           "- Open Diagnostics: it shows whether the engine was found and every hook installed.\n"
           "- The log file is OutlastModMenu\\OutlastModMenu.log next to OLGame.exe.\n"
           "- Some features depend on what is loaded: e.g. a model can only be swapped in when the game has it in "
           "memory (visit the area where that character appears, or load it from a mod package)."}}},

        {"Survival basics",
         {{"You can't fight",
           "Outlast's own intro says it: you are not a fighter - your only choices are to run, hide, or die. "
           "Avoid being seen in the first place."},
          {"Running away works",
           "- You run at 450 units/s. Most pursuers chase at 325 (Normal) and 375 (Hard), so in a straight corridor "
           "you pull away.\n"
           "- Running backwards or sideways is slower (35% / 20% speed penalty) - turn and run forward.\n"
           "- Hold the lean keys while running to look behind you without slowing down.\n"
           "- Watch out: a few patient variants sprint at 400-450 and some Walrider forms reach 475-600."},
          {"Doors and obstacles",
           "- Closing doors behind you slows pursuers (game tutorial). Most enemies need 3 bashes to break a door, "
           "Chris Walker only 2, some patients 5.\n"
           "- Look for pushable objects to block doors (hold Use beside them).\n"
           "- Vaulting, squeezing through gaps and climbing are faster for you than for most enemies."},
          {"Noise",
           "Every move makes noise that enemies hear inside their hearing range (2000 units on Normal, 3000 on "
           "Hard).\n"
           "- Running: loudness 1.0, walking: 0.3, crouch-walking: 0.1 - crouching is 10x quieter than running.\n"
           "- Slamming a door open is loud (0.6); opening it slowly by holding Use is half as loud (0.3).\n"
           "- Water makes walking louder (0.45)."},
          {"Darkness and light",
           "- An enemy that hasn't noticed you sees up to 5000 units in a narrow cone when the area is lit, but "
           "only about 400 units in the dark.\n"
           "- Crouching halves the distance at which you are spotted.\n"
           "- Night vision lets you see in the dark - enemies don't see you any better because of it.\n"
           "- Once an enemy is hunting you, darkness helps much less."},
          {"Health",
           "- Health regenerates 10 seconds after you were last hit, at 10 HP per second.\n"
           "- Falls hurt above a falling speed of 1250 and kill above 1500.\n"
           "- When grabbed, shake the mouse left and right (or the right stick) to break free."}}},

        {"Hiding",
         {{"Lockers and beds",
           "- Hide in lockers or under beds (Use). The game's tutorial: 'Hide in the locker. Don't try to fight.'\n"
           "- Searching enemies strongly prefer to check lockers and beds (weighted 10x over other spots for "
           "patients, 7x for Chris Walker, 9x for Trager and the Groom).\n"
           "- When a patient checks your hiding spot there is a 10% chance it pulls you out.\n"
           "- Chris Walker can only pull you out of hiding from the Sewers onward, Dr. Trager after the "
           "operating-room scene. Eddie Gluskin never pulls you out (0%).\n"
           "- The game avoids finding you twice in a row."},
          {"When to leave",
           "Peek with the lean keys before stepping out. Enemies give up a chase after losing sight of you for "
           "1-1.5 seconds (3 seconds for the Groom), then investigate the area - wait until their footsteps fade."}}},

        {"Batteries & camcorder",
         {{"Battery basics",
           "- One battery powers the night vision for 150 seconds (Normal and Hard).\n"
           "- You can carry 10 batteries on Normal, 5 on Hard and 2 on Nightmare/Insane. You start with 2.\n"
           "- Reload with R. Only reload when the current one is nearly empty - the remaining charge is lost.\n"
           "- The image starts to glitch in the last 25 seconds of a battery."},
          {"Saving charge",
           "- Night vision only drains while it is ON: switch it off as soon as you can see.\n"
           "- The camcorder itself (zoom, recording) does not use battery.\n"
           "- Batteries glow faintly; the ESP tab can highlight every battery in the level."},
          {"Notes (recordings)",
           "Notes are only added to your notebook when the camcorder is raised while an event happens. "
           "Recording spots are invisible markers: the ESP tab shows every one and whether it is already recorded."}}},

        {"Enemies",
         {{"Chris Walker",
           "The giant former soldier. Hits for 51 on Normal (two hits) and 101 on Hard (one hit). Breaks doors in "
           "2 bashes. Chases at 325 (350-375 for some encounters). He searches hiding spots from the Sewers on."},
          {"Dr. Richard Trager",
           "The 'surgeon' of the Male Ward. Patrols faster than most (180) and chases at 325 (350 on Hard). Hits "
           "for 34 on Normal, 60 on Hard."},
          {"Patients",
           "Most variants hit for 30 (60 on Hard) and chase at 325. Some are slow wanderers (60-120), some sprint at "
           "400-450. A couple of variants kill in one hit (101 damage) but can barely see or hear (150-400 units) - "
           "keep your distance and stay quiet."},
          {"The Walrider",
           "Sees everything around it (179-degree vision cone, 50000-unit range) and does not need to search "
           "hiding spots. 75 damage on Normal, 110 on Hard. Some forms move at 475-600, faster than you. Keep "
           "moving toward the objective."},
          {"Eddie Gluskin (Whistleblower)",
           "'The Groom'. Hits very hard (90 on Normal, 110 on Hard), reacts instantly once he notices you and keeps "
           "looking for 3 seconds after losing you. He doesn't avoid routes through closed doors - but he never "
           "pulls you out of a hiding spot."},
          {"Frank Manera (Whistleblower)",
           "The cannibal. 49 damage on Normal, 90 on Hard. He chases at 300 - slower than you - and, like the "
           "Groom, doesn't avoid routes through closed doors."},
          {"Father Martin",
           "Not hostile. He guides Miles through the asylum."}}},

        {"Chapter objectives - Outlast",
         {{"Administration Block",
           "- Investigate Mount Massive Asylum.\n"
           "- Find the keycard to unlock Security Control, then go to the security control room.\n"
           "- Restart the generator in the basement: retrieve a fuse from the Storage Room, turn on the 2 gas pumps "
           "and the Main Breaker.\n"
           "- Return to Security Control to unlock the main doors."},
          {"Prison Block",
           "- Follow blood trails to the exit.\n"
           "- Exit through the Showers (entrance needs a key card); find an alternate path when the way is blocked."},
          {"Sewer",
           "- Find a way out of the sewers.\n"
           "- Flush the water by turning the 2 valves, then use the ladder to reach the Lower Junction."},
          {"Male Ward",
           "- Reach the ground floor, escape from Trager.\n"
           "- Find the key to unlock the elevator, then find a way out of the elevator.\n"
           "- Turn on the 2 water valves, then the sprinkler system to put out the fire.\n"
           "- Find Father Martin outside."},
          {"Courtyard", "- Look for Father Martin. The Maintenance Shed needs a key."},
          {"Female Ward",
           "- Find Father Martin inside the Female Ward; use the upper floors.\n"
           "- Find the key of the main stairs to reach the 3rd floor.\n"
           "- The laundry chute needs three fuses.\n"
           "- Follow the blood. You need your camcorder to navigate the darkness."},
          {"Return to the Administration Block",
           "- Retrieve the key from the Recreation Hall.\n"
           "- Find Father Martin on the 3rd floor.\n"
           "- The elevator will take you to the main exit."},
          {"Underground Lab",
           "- Find another way out.\n"
           "- Find Billy in the main laboratory.\n"
           "- Turn off the valve for the LIFE SUPPORT FLUID RESERVOIR, cut the electric supply from the SUBLAB "
           "GENERATOR, and disable Billy's Life Pod failsafe.\n"
           "- Get out."}}},

        {"Chapter objectives - Whistleblower",
         {{"Underground Lab", "- Report to the main console at the Morphogenic Engine.\n"
                              "- Retrieve your laptop from the Server Room."},
          {"Hospital",
           "- Unlock the handcuffs to open the door (find the handcuff key).\n"
           "- Find the main valve and shut off the gas to access the airlock."},
          {"Prison", "- Use the short-wave radio in the Prison.\n- Exit via the Administration Block."},
          {"Drying Ground", "- Turn off the electricity."},
          {"Vocational Block", "- Find the key to access the Male Ward."}}},

        {"Achievements",
         {{"Outlast",
           "- Illuminated: restore power to the Administration Block.\n"
           "- Flushed: drain the Sewers.\n"
           "- Soaked: activate the sprinklers in the Male Ward.\n"
           "- Emancipated: collect the key in the Female Ward.\n"
           "- Educated: collect 15 documents and complete 15 recordings.\n"
           "- Pulitzer: collect all documents and complete all recordings.\n"
           "- Punished: finish the game.\n"
           "- Lunatic: finish the game in Insane mode."},
          {"Whistleblower",
           "- Gas Leaker: turn off the gas.\n"
           "- Shocker: turn off the electricity.\n"
           "- Whistleblower: finish the DLC.\n"
           "- Bowelwhistler: finish the DLC in Insane mode.\n"
           "- Archivist: collect all documents.\n"
           "- Legacy: complete all recordings."},
          {"Console-only trophies",
           "The PS4/Xbox One versions add Elevator Operator (drop the corpse in the elevator shaft), Claustrophobe "
           "(finish without hiding under a bed or in a locker) and Energiser (finish Insane without reloading "
           "batteries). They exist in the game code but are not on Steam."},
          {"Collectible hunting",
           "- Turn on ESP for Documents and Recording spots: documents show until picked up, recording spots show "
           "whether they are recorded.\n"
           "- The Teleport tab lists the documents, recording spots and batteries around you, with a button to "
           "teleport to each.\n"
           "- Notes need the camcorder raised near the event for long enough - stand still and keep recording."}}},

        {"Insane mode",
         {{"The rules",
           "Insane is the hardest setting: no checkpoints - if you die, all progress is lost and you start over. "
           "You can carry only 2 batteries and enemies hit for their Hard-difficulty damage (Chris Walker and "
           "special patients kill in one hit)."},
          {"Tips",
           "- Raise the gamma (Visuals tab or the game's own setting) to navigate dark areas without night vision.\n"
           "- Learn every chase route first: use the checkpoint loader and god mode to practise sections, then play "
           "them clean.\n"
           "- Crouch-walk past unaware enemies and never open doors loudly near them.\n"
           "- Save batteries for the sections that require night vision (dark basements, the Female Ward without "
           "light)."}}},

        {"Practice & speedrunning",
         {{"Practice tools",
           "- Load any checkpoint (Teleport tab) to repeat a chase or puzzle.\n"
           "- Save/restore your position with the hotkeys (F6/F7 by default).\n"
           "- Slow the game down (World tab) to study enemy behaviour."},
          {"Rules",
           "Speedrun.com's Outlast community allows only 'stat fps' among the debug commands in official runs - "
           "anything else from this menu gets a run rejected. Turn the mod off (remove dinput8.dll) for real runs."},
          {"Useful console commands",
           "Type these in the Console box (Diagnostics tab):\n"
           "- stat fps - frame-rate counter.\n"
           "- displayall OLGame CurrentCheckpointName - shows the current checkpoint on screen.\n"
           "- cp <CheckpointName> - start at a checkpoint.\n"
           "- ToggleFreeCam / Ghost / God / ToggleUnlimitedBatteries - developer cheats."}}},
    };
    return guide;
}

}  // namespace omm::data
