"""Apply the frozen, explicit landmark delta; never reinstall the campus level."""
import json
import shutil
import sys
from pathlib import Path
from datetime import datetime
import unreal as u
sys.path.insert(0,str(Path(__file__).resolve().parent))
from landmark_contract import merge_shapes
from landmark_state import capture, shape_data

P=Path(r'D:/0bay/OfficeShell')
OUT=P/'Saved/HaijuNorthBeta'
SOURCE=P/'Art/HaijuNorthBeta/Landmarks'
ROOT='/Game/HaijuNorthBeta/Landmarks'
EXTERIOR='/Game/HaijuNorthBeta/Maps/L_HaijuNorthBeta_Exterior'
CAMPUS='/Game/HaijuNorthBeta/Maps/L_Campus500_Details'
SUB='/Game/HaijuNorthBeta/Maps/L_RefinedLandmarks'
MAIN='/Game/OfficeShell/Maps/Office_Demo'
anchor=u.Vector(14485.456288654983,-5394.3765929212095,0)
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
lib=u.EditorAssetLibrary
tools=u.AssetToolsHelpers.get_asset_tools()
manifest=json.loads((SOURCE/'replacement_manifest.json').read_text(encoding='utf-8'))
backup=OUT/('LandmarkBackup_'+datetime.now().strftime('%Y%m%d_%H%M%S'))
backup.mkdir(parents=True)
for path in (EXTERIOR,CAMPUS,SUB,MAIN,'/Game/OfficeShell/Maps/L_GrandNeoBay_Building'):
    source=P/'Content'/(path.removeprefix('/Game/')+'.umap')
    if source.exists():shutil.copy2(source,backup)
assert levels.load_level(MAIN)
baseline=OUT/'landmark_baseline.json'
if not baseline.exists():baseline.write_text(json.dumps(capture(),indent=2),encoding='utf-8')
shutil.copy2(baseline,backup)

def import_glb(name,count):
    task=u.AssetImportTask();task.filename=str(SOURCE/(name+'.glb'))
    task.destination_path=ROOT+'/Imported';task.automated=True;task.save=True;task.replace_existing=True
    tools.import_asset_tasks([task])
    meshes={}
    for data in u.AssetRegistryHelpers.get_asset_registry().get_assets_by_path(ROOT+'/Imported/'+name,recursive=True):
        obj=lib.load_asset(str(data.package_name))
        if isinstance(obj,u.StaticMesh):meshes[obj.get_name()]=obj
    assert len(meshes)==count,(name,len(meshes))
    return meshes

remaining=import_glb('Background_Changed_Batches',9)
landmarks=import_glb('Landmarks_Combined',48)
assert levels.load_level(EXTERIOR)
existing={a.static_mesh_component.static_mesh.get_name():a for a in actors.get_all_level_actors() if isinstance(a,u.StaticMeshActor)}
assert len(existing)==158
changes=[]
for item in manifest['batch_replacements']:
    a=existing[item['old_mesh_name']];c=a.static_mesh_component
    if item['replacement_mesh_name']:
        materials=[c.get_material(i) for i in range(c.get_num_materials())]
        c.set_static_mesh(remaining[item['replacement_mesh_name']])
        # Retain the exact original UE materials, including embedded facade textures.
        assert c.get_num_materials()==len(materials)
        for i,m in enumerate(materials):c.set_material(i,m)
        c.set_visibility(True);a.set_actor_hidden_in_game(False)
    else:
        c.set_visibility(False);a.set_actor_hidden_in_game(True)
    changes.append(a.get_actor_label())
assert levels.save_current_level()

if lib.does_asset_exist(SUB):assert levels.load_level(SUB)
else:assert levels.new_level(SUB)
owned={a.get_actor_label():a for a in actors.get_all_level_actors()}
for name,mesh in sorted(landmarks.items()):
    label='Landmark_'+name
    a=owned.get(label) or actors.spawn_actor_from_class(u.StaticMeshActor,anchor,u.Rotator(yaw=20.9))
    a.set_actor_label(label);a.set_folder_path('RefinedLandmarks')
    c=a.static_mesh_component;c.set_static_mesh(mesh)
    c.set_editor_property('mobility',u.ComponentMobility.STATIC)
    c.set_collision_profile_name('NoCollision')
    for prop in ('generate_overlap_events','can_ever_affect_navigation','cast_shadow','affect_distance_field_lighting'):
        c.set_editor_property(prop,False)
assert levels.save_current_level()

assert levels.load_level(CAMPUS)
managers=[a for a in actors.get_all_level_actors() if a.get_class().get_name()=='CampusInteraction']
assert len(managers)==1
manager=managers[0]
data=merge_shapes([shape_data(s) for s in manager.get_editor_property('shapes')])
shapes=[]
for item in data:
    shape=u.CampusBuildingShape()
    shape.set_editor_property('key',item['key'])
    for field in ('vertices','edges'):shape.set_editor_property(field,[u.Vector(*p) for p in item[field]])
    shape.set_editor_property('triangles',item['triangles']);shapes.append(shape)
manager.set_editor_property('shapes',shapes)
assert levels.save_current_level()

assert levels.load_level(MAIN)
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
stream=u.GameplayStatics.get_streaming_level(world,SUB)
if not stream:stream=u.EditorLevelUtils.add_level_to_world(world,SUB,u.LevelStreamingAlwaysLoaded)
assert stream and isinstance(stream,u.LevelStreamingAlwaysLoaded)
assert levels.set_current_level_by_name('Office_Demo')
assert levels.save_current_level()
report=dict(status='complete',backup=str(backup),changed_batches=changes,new_mesh_actors=len(landmarks),
            total_pick_shapes=len(shapes),original_materials_reused=True,sublevel=SUB)
(OUT/'landmark_install_report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
u.log('LANDMARK_INSTALL_COMPLETE '+json.dumps(report))
