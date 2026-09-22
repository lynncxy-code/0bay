"""Read-only persistent-level and placement regression checks."""
import json
import re
from pathlib import Path
import unreal as u

OUT = Path(u.Paths.project_saved_dir()) / 'GrandNeoBay'
before = json.loads((OUT/'scene_inventory_before.json').read_text(encoding='utf-8'))
levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert levels.load_level('/Game/OfficeShell/Maps/Office_Demo')
actors = u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
def vec(v): return [v.x, v.y, v.z]
after = {(a.get_outer().get_path_name(), a.get_name()): a for a in actors}
changes = []
for old in before:
    actor = after.get((old['level'], old['name']))
    if not actor:
        changes.append({'name':old['name'], 'reason':'missing'})
        continue
    for key, value in [('location',vec(actor.get_actor_location())), ('scale',vec(actor.get_actor_scale3d()))]:
        if any(abs(a-b) > .001 for a,b in zip(value,old[key])):
            changes.append({'name':old['name'], 'reason':key, 'before':old[key], 'after':value})
    if actor.get_class().get_path_name() != old['cls']:
        changes.append({'name':old['name'], 'reason':'class'})
    rotation = actor.get_actor_rotation()
    for key in ('pitch', 'yaw', 'roll'):
        match = re.search(key+r': ([-0-9.]+)', old['rotation'])
        if match and abs(float(match.group(1))-getattr(rotation,key)) > .001:
            changes.append({'name':old['name'], 'reason':'rotation.'+key})
experience_class = u.load_class(None, '/Script/OfficeHost.GrandNeoBayExperience')
experiences = [a for a in actors if a.get_class()==experience_class]
assert len(experiences)==1, len(experiences)
experience = experiences[0]
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
stream = u.GameplayStatics.get_streaming_level(world, '/Game/OfficeShell/Maps/L_GrandNeoBay_Building')
assert stream and isinstance(stream, u.LevelStreamingAlwaysLoaded)
assert abs(experience.get_editor_property('floor_root').get_world_location().z-4320)<1
assert experience.get_editor_property('building').get_editor_property('child_actor')
assert experience.get_editor_property('floor_volume').static_mesh
component_names = [component.get_name() for component in experience.get_components_by_class(u.StaticMeshComponent)]
assert 'BuildingBase' not in component_names, component_names
assert not u.EditorAssetLibrary.does_asset_exist('/Game/GrandNeoBay/Interaction/M_GNB_Base')
report = dict(passed=not changes, preserved_original_actors=len(before), transform_or_class_changes=changes,
              experience_actor_count=len(experiences), building_streaming_class=stream.get_class().get_name(),
              floor_world_z=experience.get_editor_property('floor_root').get_world_location().z,
              gray_building_base_removed=True)
report['overview_distance_cm'] = experience.get_editor_property('overview_distance')
report['overview_camera_relative_location'] = vec(experience.get_editor_property('overview_camera').get_editor_property('relative_location'))
(OUT/'install_verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
assert not changes, changes
u.log('GNB_VERIFY_COMPLETE '+json.dumps(report))
