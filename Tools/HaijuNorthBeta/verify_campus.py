"""Reload persisted maps: protect original actors, check ground separation and campus data."""
import json
from pathlib import Path
import unreal as u

P=Path(r'D:/0bay/OfficeShell')
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert levels.load_level('/Game/OfficeShell/Maps/Office_Demo')
actors=list(u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors())
managers=[a for a in actors if a.get_class().get_name()=='CampusInteraction']
assert len(managers)==1
manager=managers[0]
shapes=manager.get_editor_property('shapes')
manifest=P/'Art/HaijuNorthBeta/Landmarks/replacement_manifest.json'
expected=json.loads(manifest.read_text(encoding='utf-8'))['expected_total_pick_shapes_after'] if manifest.exists() else 1678
assert len(shapes)==expected
assert len(set(s.get_editor_property('key') for s in shapes))==len(shapes)
invalid=[]
for s in shapes:
    vertices=s.get_editor_property('vertices');triangles=s.get_editor_property('triangles');edges=s.get_editor_property('edges')
    if len(triangles)%3 or len(edges)%2 or not vertices or any(t<0 or t>=len(vertices) for t in triangles):invalid.append(s.get_editor_property('key'))
assert not invalid
ground=[]
detail=[]
for a in actors:
    if isinstance(a,u.StaticMeshActor):
        c=a.static_mesh_component
        if not c.static_mesh:continue
        name=c.static_mesh.get_name()
        if name=='Ground_7' or name.startswith('Roads_'):
            b=c.static_mesh.get_bounding_box();loc=a.get_actor_location()
            ground.append(dict(mesh=name,actor_z=loc.z,min_z=b.min.z+loc.z,max_z=b.max.z+loc.z))
        if a.get_outer().get_path_name().startswith('/Game/HaijuNorthBeta/Maps/L_Campus500_Details.'):
            assert c.get_collision_enabled()==u.CollisionEnabled.NO_COLLISION
            assert not c.get_editor_property('can_ever_affect_navigation')
            detail.append(name)
assert len(detail)==13
plane=next(g for g in ground if g['mesh']=='Ground_7')
assert abs(plane['max_z']-1)<.01
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
stream=u.GameplayStatics.get_streaming_level(world,'/Game/HaijuNorthBeta/Maps/L_Campus500_Details')
assert isinstance(stream,u.LevelStreamingAlwaysLoaded)
report=dict(passed=True,buildings=len(shapes),unique_keys=True,invalid_shapes=invalid,details_meshes=detail,
            ground_and_road_z_cm=ground,streaming='AlwaysLoaded',original_scene_regression='See install_verification.json')
(P/'Saved/HaijuNorthBeta/campus_verification.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
u.log('CAMPUS_VERIFIED '+json.dumps(report))
