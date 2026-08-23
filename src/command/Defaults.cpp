#include "Defaults.h"
#include "defaults/StopCommand.h"
#include "defaults/HelpCommand.h"
#include "defaults/VersionCommand.h"
#include "defaults/KickCommand.h"
#include "defaults/TeleportCommand.h"
#include "defaults/GamemodeCommand.h"
#include "defaults/GiveCommand.h"
#include "defaults/SayCommand.h"
#include "defaults/ListCommand.h"
#include "defaults/OpCommand.h"
#include "defaults/DeopCommand.h"
#include "defaults/TimeCommand.h"
#include "defaults/WeatherCommand.h"
#include "defaults/SaveCommand.h"
#include <memory>
namespace mcppeserver {
void registerDefaultCommands(CommandMap& map) {
    map.registerCommand(std::make_unique<StopCommand>());
    map.registerCommand(std::make_unique<HelpCommand>("help"));
    map.registerCommand(std::make_unique<HelpCommand>("?"));
    map.registerCommand(std::make_unique<VersionCommand>());
    map.registerCommand(std::make_unique<KickCommand>());
    map.registerCommand(std::make_unique<TeleportCommand>("teleport"));
    map.registerCommand(std::make_unique<TeleportCommand>("tp"));
    map.registerCommand(std::make_unique<GamemodeCommand>("gamemode"));
    map.registerCommand(std::make_unique<GamemodeCommand>("gm"));
    map.registerCommand(std::make_unique<GiveCommand>());
    map.registerCommand(std::make_unique<SayCommand>());
    map.registerCommand(std::make_unique<ListCommand>());
    map.registerCommand(std::make_unique<OpCommand>());
    map.registerCommand(std::make_unique<DeopCommand>());
    map.registerCommand(std::make_unique<TimeCommand>());
    map.registerCommand(std::make_unique<WeatherCommand>());
    map.registerCommand(std::make_unique<SaveCommand>());
}
} // namespace
