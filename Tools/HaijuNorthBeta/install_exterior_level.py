"""Install the complete Haiju North GLB as an always-loaded background sublevel.

The imported meshes keep their source-global vertices. Every actor therefore uses
the same fitted anchor transform. No actor that predates this script is edited.
"""
import json
import traceback
from pathlib import Path
import unreal as u

PROJECT = Path(u.Paths.convert_relative_path_to_full(u.Paths.project_dir())).resolve()
OUT = PROJECT / 'Saved' / 'HaijuNorthBeta'
OUT.mkdir(parents=True, exist_ok=True)
ASSET_ROOT = '/Game/HaijuNorthBeta/Imported/Haiju_North_Environment'
MESH_ROOT = ASSET_ROOT + '/StaticMeshes'
SUBLEVEL = '/Game/HaijuNorthBeta/Maps/L_HaijuNorthBeta_Exterior'
MAIN_LEVEL = '/Game/OfficeShell/Maps/Office_Demo'

# Visual registration against the Office satellite Landscape. The source origin
# is the approximate WGS84 anchor; yaw follows the Landscape local X/east axis.
ANCHOR_CM = u.Vector(14485.456288654983, -5394.3765929212095, 0.0)
YAW_DEG = 20.9

lib = u.EditorAssetLibrary
registry = u.AssetRegistryHelpers.get_asset_registry()
levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
actor_subsystem = u.get_editor_subsystem(u.EditorActorSubsystem)
report = {'status': 'running'}

def vec(value):
    return [value.x, value.y, value.z]

try:
    assert PROJECT == Path(r'D:/0bay/OfficeShell').resolve(), PROJECT
    mesh_assets = []
    for data in registry.get_assets_by_path(MESH_ROOT, recursive=True):
        obj = lib.load_asset(str(data.package_name))
        if isinstance(obj, u.StaticMesh):
            mesh_assets.append(obj)
    mesh_assets.sort(key=lambda obj: obj.get_path_name())
    assert len(mesh_assets) == 158, len(mesh_assets)

    if lib.does_asset_exist(SUBLEVEL):
        assert levels.load_level(SUBLEVEL)
        existing = list(actor_subsystem.get_all_level_actors())
        if existing:
            assert actor_subsystem.destroy_actors(existing)
    else:
        assert levels.new_level(SUBLEVEL)

    spawned = []
    for index, mesh in enumerate(mesh_assets):
        actor = actor_subsystem.spawn_actor_from_class(u.StaticMeshActor, ANCHOR_CM, u.Rotator(yaw=YAW_DEG))
        assert actor
        actor.set_actor_label('HaijuNorthBeta_%03d_%s' % (index, mesh.get_name()))
        actor.set_folder_path('HaijuNorthBeta/Background')
        actor.set_actor_scale3d(u.Vector(1.0, 1.0, 1.0))
        actor.tags = ['HaijuNorthBetaBackground', 'NoRuntimePicking']
        component = actor.get_editor_property('static_mesh_component')
        component.set_static_mesh(mesh)
        if mesh.get_name() == 'Ground_7':
            pos = actor.get_actor_location()
            pos.z = 1.0 - mesh.get_bounding_box().max.z
            actor.set_actor_location(pos, False, False)
        component.set_editor_property('mobility', u.ComponentMobility.STATIC)
        component.set_collision_profile_name('NoCollision')
        component.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
        component.set_editor_property('generate_overlap_events', False)
        component.set_editor_property('can_ever_affect_navigation', False)
        component.set_editor_property('cast_shadow', False)
        component.set_editor_property('affect_distance_field_lighting', False)
        component.set_editor_property('render_custom_depth', False)
        spawned.append(actor)

    assert levels.save_current_level()

    assert levels.load_level(MAIN_LEVEL)
    world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    streaming = u.GameplayStatics.get_streaming_level(world, SUBLEVEL)
    if not streaming:
        streaming = u.EditorLevelUtils.add_level_to_world(world, SUBLEVEL, u.LevelStreamingAlwaysLoaded)
    assert streaming
    assert isinstance(streaming, u.LevelStreamingAlwaysLoaded)
    assert levels.set_current_level_by_name('Office_Demo')
    assert levels.save_current_level()

    report.update(
        status='complete',
        main_level=MAIN_LEVEL,
        sublevel=SUBLEVEL,
        mesh_actor_count=len(spawned),
        anchor_world_cm=vec(ANCHOR_CM),
        yaw_deg=YAW_DEG,
        scale=[1.0, 1.0, 1.0],
        collision='NoCollision',
        overlap_events=False,
        affects_navigation=False,
        casts_shadows=False,
        affects_distance_field_lighting=False,
        runtime_picking=False,
        streaming_class=streaming.get_class().get_name(),
        existing_actor_transforms_modified=False,
    )
    u.log('HAIJU_EXTERIOR_INSTALL_COMPLETE actors=%d' % len(spawned))
except Exception:
    report['status'] = 'failed'
    report['error'] = traceback.format_exc()
    u.log_error(report['error'])
    raise
finally:
    (OUT / 'install_report.json').write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')

# Full exterior rebuilds must reapply the frozen landmark delta, retaining the
# campus landscape and replacing its target pick shapes by key only.
if (PROJECT/'Art/HaijuNorthBeta/Landmarks/replacement_manifest.json').exists():
    import runpy
    runpy.run_path(str(PROJECT/'Tools/HaijuNorthBeta/install_landmarks.py'),run_name='__main__')
