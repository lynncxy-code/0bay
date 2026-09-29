"""Read current saved 0bay transforms and interaction geometry; do not save assets."""
import json
from pathlib import Path
import unreal as u

out = Path(r'D:/0bay/OfficeShell/Saved/HaijuNorthBeta/current_main_alignment.json')
levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert levels.load_level('/Game/OfficeShell/Maps/Office_Demo')
actors = u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()

def vec(v):
    return [round(v.x, 3), round(v.y, 3), round(v.z, 3)]

experience = next(a for a in actors if a.get_class().get_name() == 'GrandNeoBayExperience')
campus = next(a for a in actors if a.get_class().get_name() == 'CampusInteraction')
building_component = experience.get_editor_property('building')
building = building_component.get_editor_property('child_actor')
floor_root = experience.get_editor_property('floor_root')
floor_volume = experience.get_editor_property('floor_volume')
report = {
    'experience_location': vec(experience.get_actor_location()),
    'experience_rotation': str(experience.get_actor_rotation()),
    'building_component_location': vec(building_component.get_world_location()),
    'floor_offset': vec(experience.get_editor_property('floor_offset')),
    'floor_height': experience.get_editor_property('floor_height'),
    'floor_root_world': vec(floor_root.get_world_location()),
    'floor_volume_world': vec(floor_volume.get_world_location()),
    'campus_location': vec(campus.get_actor_location()),
    'campus_rotation': str(campus.get_actor_rotation()),
    'main_shape': [],
    'building': None,
}
for shape in campus.get_editor_property('shapes'):
    if shape.get_editor_property('key') != 'GrandNeoBay':
        continue
    vertices = shape.get_editor_property('vertices')
    report['main_shape'] = {
        'count': len(vertices),
        'local_min': [min(getattr(v, axis) for v in vertices) for axis in 'xyz'],
        'local_max': [max(getattr(v, axis) for v in vertices) for axis in 'xyz'],
    }
    break
if building:
    center, extent = building.get_actor_bounds(False)
    report['building'] = {
        'location': vec(building.get_actor_location()),
        'rotation': str(building.get_actor_rotation()),
        'bounds_center': vec(center),
        'bounds_extent': vec(extent),
        'mesh_components': [],
    }
    for component in building.get_components_by_class(u.StaticMeshComponent):
        if not component.get_editor_property('static_mesh'):
            continue
        report['building']['mesh_components'].append({
            'name': component.get_name(),
            'mesh': component.get_editor_property('static_mesh').get_name(),
            'location': vec(component.get_world_location()),
            'relative_location': vec(component.get_editor_property('relative_location')),
        })
out.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
u.log('CURRENT_MAIN_ALIGNMENT ' + json.dumps(report, ensure_ascii=False))
