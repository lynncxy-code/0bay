"""Read-only validation for the robot-patrol streaming-level contract."""

import json

import unreal as u


MAIN_LEVEL = "/Game/OfficeShell/Maps/Office_Demo"
SUBLEVEL = "/Game/RobotPatrol/Maps/L_RobotPatrol"
FEATURE_ROOT_LABEL = "Robot Patrol - runtime feature root"

levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
actors = u.get_editor_subsystem(u.EditorActorSubsystem)

assert levels.load_level(MAIN_LEVEL)
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
streaming = u.GameplayStatics.get_streaming_level(world, SUBLEVEL)
assert isinstance(streaming, u.LevelStreamingAlwaysLoaded)
streaming_class = streaming.get_class().get_name()

assert levels.load_level(SUBLEVEL)
feature_roots = [
    actor
    for actor in actors.get_all_level_actors()
    if actor.get_actor_label() == FEATURE_ROOT_LABEL
]
assert len(feature_roots) == 1

report = {
    "main_level": MAIN_LEVEL,
    "sublevel": SUBLEVEL,
    "streaming_class": streaming_class,
    "feature_root_count": len(feature_roots),
}
u.log("ROBOT_PATROL_SUBLEVEL_VERIFIED " + json.dumps(report))
