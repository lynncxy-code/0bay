"""Create the robot-patrol streaming level without placing actors in Office_Demo."""

from pathlib import Path
import shutil

import unreal as u


MAIN_LEVEL = "/Game/OfficeShell/Maps/Office_Demo"
SUBLEVEL = "/Game/RobotPatrol/Maps/L_RobotPatrol"
FEATURE_ROOT_LABEL = "Robot Patrol - runtime feature root"

levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
actors = u.get_editor_subsystem(u.EditorActorSubsystem)
assets = u.EditorAssetLibrary

project_dir = Path(u.Paths.project_dir())
backup_dir = project_dir / "Saved" / "RobotPatrol"
backup_dir.mkdir(parents=True, exist_ok=True)
shutil.copy2(
    project_dir / "Content" / "OfficeShell" / "Maps" / "Office_Demo.umap",
    backup_dir / "Office_Demo.before_robot_patrol.umap",
)

if assets.does_asset_exist(SUBLEVEL):
    assert levels.load_level(SUBLEVEL)
else:
    assert levels.new_level(SUBLEVEL)

feature_roots = [
    actor
    for actor in actors.get_all_level_actors()
    if actor.get_actor_label() == FEATURE_ROOT_LABEL
]
if not feature_roots:
    feature_root = actors.spawn_actor_from_class(u.Actor, u.Vector(0.0, 0.0, 4320.0))
    feature_root.set_actor_label(FEATURE_ROOT_LABEL)
    feature_root.set_folder_path("RobotPatrol")
    feature_root.tags = [u.Name("RobotPatrolFeatureRoot")]

assert levels.save_current_level()
assert levels.load_level(MAIN_LEVEL)

world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
streaming = u.GameplayStatics.get_streaming_level(world, SUBLEVEL)
if not streaming:
    streaming = u.EditorLevelUtils.add_level_to_world(
        world, SUBLEVEL, u.LevelStreamingAlwaysLoaded
    )

assert isinstance(streaming, u.LevelStreamingAlwaysLoaded)
assert levels.set_current_level_by_name("Office_Demo")
assert levels.save_current_level()
u.log(f"ROBOT_PATROL_SUBLEVEL_READY {SUBLEVEL}")
