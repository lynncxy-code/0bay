"""Read-only regression checks for the Haiju North exterior sublevel."""
import json
from pathlib import Path
import unreal as u

OUT = Path(u.Paths.project_saved_dir()) / 'HaijuNorthBeta'
before = json.loads((OUT / 'scene_inventory_before.json').read_text(encoding='utf-8'))
MAIN_LEVEL = '/Game/OfficeShell/Maps/Office_Demo'
SUBLEVEL = '/Game/HaijuNorthBeta/Maps/L_HaijuNorthBeta_Exterior'

levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert levels.load_level(MAIN_LEVEL)
all_actors = u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
after = {(a.get_outer().get_path_name(), a.get_name()): a for a in all_actors}

def vec(value):
    return [value.x, value.y, value.z]

changes = []
for old in before:
    actor = after.get((old['level'], old['name']))
    if actor is None:
        changes.append({'name': old['name'], 'level': old['level'], 'reason': 'missing'})
        continue
    values = {
        'location': vec(actor.get_actor_location()),
        'rotation': [actor.get_actor_rotation().pitch, actor.get_actor_rotation().yaw, actor.get_actor_rotation().roll],
        'scale': vec(actor.get_actor_scale3d()),
    }
    for key, value in values.items():
        if any(abs(a - b) > .001 for a, b in zip(value, old[key])):
            changes.append({'name': old['name'], 'level': old['level'], 'reason': key,
                            'before': old[key], 'after': value})
    if actor.get_class().get_path_name() != old['class']:
        changes.append({'name': old['name'], 'level': old['level'], 'reason': 'class'})

world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
streaming = u.GameplayStatics.get_streaming_level(world, SUBLEVEL)
assert streaming and isinstance(streaming, u.LevelStreamingAlwaysLoaded)
sub_actors = [a for a in all_actors if a.get_outer().get_path_name().startswith(SUBLEVEL + '.')]
assert len(sub_actors) == 158, len(sub_actors)

component_failures = []
origin = u.Vector(14485.456288654983, -5394.3765929212095, 0.0)
for actor in sub_actors:
    expected = u.Vector(origin.x, origin.y, 3.0 if 'Ground_7' in actor.get_actor_label() else 0.0)
    if actor.get_actor_location().distance(expected) > .01 or abs(actor.get_actor_rotation().yaw - 20.9) > .001:
        component_failures.append({'actor': actor.get_actor_label(), 'reason': 'transform'})
    component = actor.get_editor_property('static_mesh_component')
    if component.get_collision_profile_name() != 'NoCollision' or component.get_collision_enabled() != u.CollisionEnabled.NO_COLLISION:
        component_failures.append({'actor': actor.get_actor_label(), 'reason': 'collision',
                                   'profile': component.get_collision_profile_name(),
                                   'enabled': str(component.get_collision_enabled())})
    if component.get_editor_property('can_ever_affect_navigation'):
        component_failures.append({'actor': actor.get_actor_label(), 'reason': 'navigation'})
    if component.get_editor_property('generate_overlap_events'):
        component_failures.append({'actor': actor.get_actor_label(), 'reason': 'overlap'})
    if component.get_editor_property('cast_shadow'):
        component_failures.append({'actor': actor.get_actor_label(), 'reason': 'cast_shadow'})
    if component.get_editor_property('affect_distance_field_lighting'):
        component_failures.append({'actor': actor.get_actor_label(), 'reason': 'distance_field_lighting'})

report = {
    'passed': not changes and not component_failures,
    'preserved_existing_actors': len(before),
    'existing_transform_or_class_changes': changes,
    'sublevel_actor_count': len(sub_actors),
    'sublevel_streaming_class': streaming.get_class().get_name(),
    'background_component_failures': component_failures,
    'anchor_world_cm': vec(origin),
    'yaw_deg': 20.9,
    'alignment_method': 'best-fit against Office satellite mosaic using building footprints, roads, and stadium control',
    'stadium_control_residual_pixels': 10.77,
    'meters_per_pixel': 3.5228446530220996,
    'estimated_control_residual_m': 37.94,
    'survey_grade': False,
}
(OUT / 'install_verification.json').write_text(
    json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
assert report['passed'], report
u.log('HAIJU_EXTERIOR_VERIFY_COMPLETE ' + json.dumps(report))
