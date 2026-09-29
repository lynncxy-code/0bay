"""Set Office_Demo (or the currently open map) to OfficeVRGameMode.

In the Unreal Editor Output Log:
    py F:/Office_VR/Scripts/SetupOfficeVR.py
Then save the level.
"""

import unreal


def _load_vr_game_mode():
    cls = getattr(unreal, "OfficeVRGameMode", None)
    if cls is not None:
        return cls.static_class()
    loaded = unreal.load_class(None, "/Script/OfficeHost.OfficeVRGameMode")
    if loaded:
        return loaded
    raise RuntimeError("OfficeVRGameMode not found. Compile OfficeHost C++ first.")


def main():
    gm = _load_vr_game_mode()
    editor_sub = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = editor_sub.get_editor_world()
    world_settings = world.get_world_settings()
    world_settings.set_editor_property("default_game_mode", gm)

    starts = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.PlayerStart)
    unreal.log("OfficeVR: GameMode -> OfficeVRGameMode")
    unreal.log("OfficeVR: PlayerStart count = {}".format(len(starts)))
    if len(starts) == 0:
        unreal.log_warning("OfficeVR: no PlayerStart in this map. Add one at the company gate.")
    else:
        loc = starts[0].get_actor_location()
        unreal.log("OfficeVR: first PlayerStart at {}".format(loc))

    removed = 0
    for actor in unreal.EditorLevelLibrary.get_all_level_actors():
        cls = actor.get_class()
        name = cls.get_name() if cls else ""
        if any(token in name for token in (
            "TwinSceneManager",
            "TwinGodViewAnchor",
            "TwinRoamingSpawnAnchor",
            "TwinMinimapAnchor",
        )):
            unreal.EditorLevelLibrary.destroy_actor(actor)
            removed += 1
    unreal.log("OfficeVR: removed {} OntoTwin camera/manager actors".format(removed))

    level_sub = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    level_sub.save_current_level()
    unreal.log("OfficeVR: current level saved.")

    try:
        import PlaceAgentSlots
    except ImportError:
        import importlib.util
        spec = importlib.util.spec_from_file_location(
            "PlaceAgentSlots", r"F:/Office_VR/Scripts/PlaceAgentSlots.py")
        PlaceAgentSlots = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(PlaceAgentSlots)
    PlaceAgentSlots._place(world)
    level_sub.save_current_level()
    unreal.log("OfficeVR: agent slots placed.")


if __name__ == "__main__":
    main()
