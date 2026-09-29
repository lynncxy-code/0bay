"""Install campus additions in their own always-loaded level; preserve original scene actors."""
import json
import shutil
import sys
from datetime import datetime
from pathlib import Path
import unreal as u
sys.path.insert(0,str(Path(__file__).resolve().parent))
from landmark_contract import merge_shapes

PROJECT=Path(r'D:/0bay/OfficeShell')
OUT=PROJECT/'Saved/HaijuNorthBeta'
SOURCE=PROJECT/'Art/HaijuNorthBeta/Campus500'
ROOT='/Game/HaijuNorthBeta/Campus500'
SUB='/Game/HaijuNorthBeta/Maps/L_Campus500_Details'
MAIN='/Game/OfficeShell/Maps/Office_Demo'
anchor=u.Vector(14485.456288654983,-5394.3765929212095,0)
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
lib=u.EditorAssetLibrary
tools=u.AssetToolsHelpers.get_asset_tools()
backup=OUT/('CampusBackup_'+datetime.now().strftime('%Y%m%d_%H%M%S'))
backup.mkdir(parents=True)
shutil.copy2(PROJECT/'Content/OfficeShell/Maps/Office_Demo.umap',backup)
subfile=PROJECT/'Content/HaijuNorthBeta/Maps/L_Campus500_Details.umap'
if subfile.exists():shutil.copy2(subfile,backup)

task=u.AssetImportTask()
task.filename=str(SOURCE/'Campus500.glb');task.destination_path=ROOT+'/Imported'
task.automated=True;task.save=True;task.replace_existing=True
tools.import_asset_tasks([task])
registry=u.AssetRegistryHelpers.get_asset_registry()
meshes=[]
for data in registry.get_assets_by_path(ROOT+'/Imported',recursive=True):
    asset=lib.load_asset(str(data.package_name))
    if isinstance(asset,u.StaticMesh):meshes.append(asset)
assert len(meshes)==13,len(meshes)

if lib.does_asset_exist(SUB):
    assert levels.load_level(SUB)
    # This level is exclusively owned by this installer.
    existing=list(actors.get_all_level_actors())
    if existing:assert actors.destroy_actors(existing)
else:assert levels.new_level(SUB)

for mesh in meshes:
    actor=actors.spawn_actor_from_class(u.StaticMeshActor,anchor,u.Rotator(yaw=20.9))
    actor.set_actor_label(mesh.get_name());actor.set_folder_path('Campus500/ModeledGround')
    component=actor.static_mesh_component;component.set_static_mesh(mesh)
    component.set_editor_property('mobility',u.ComponentMobility.STATIC)
    component.set_collision_profile_name('NoCollision')
    component.set_editor_property('generate_overlap_events',False)
    component.set_editor_property('can_ever_affect_navigation',False)
    component.set_editor_property('cast_shadow',False)
    component.set_editor_property('affect_distance_field_lighting',False)

cls=u.load_class(None,'/Script/OfficeHost.CampusInteraction')
assert cls,'OfficeHost must be built before installation'
manager=actors.spawn_actor_from_class(cls,anchor,u.Rotator(yaw=20.9))
manager.set_actor_label('Campus - individual building hover and selection')
manager.set_folder_path('Campus500/Interaction')
data=merge_shapes(json.loads((SOURCE/'building_shapes.json').read_text(encoding='utf-8')))
shapes=[]
for item in data:
    shape=u.CampusBuildingShape()
    shape.set_editor_property('key',item['key'])
    shape.set_editor_property('vertices',[u.Vector(*p) for p in item['vertices']])
    shape.set_editor_property('triangles',item['triangles'])
    shape.set_editor_property('edges',[u.Vector(*p) for p in item['edges']])
    shapes.append(shape)
manager.set_editor_property('shapes',shapes)
for prop,name,parent,params in [
    ('glass_material','MI_CampusGlass','M_GNB_FloorGlass',{'Opacity':.24,'Glow':1.5}),
    ('outline_material','MI_CampusOutline','M_GNB_FloorOutline',{'Glow':3.0})]:
    path=ROOT+'/'+name
    mat=lib.load_asset(path) if lib.does_asset_exist(path) else tools.create_asset(name,ROOT,u.MaterialInstanceConstant,u.MaterialInstanceConstantFactoryNew())
    u.MaterialEditingLibrary.set_material_instance_parent(mat,lib.load_asset('/Game/GrandNeoBay/Interaction/'+parent))
    for key,val in params.items():u.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mat,key,val)
    lib.save_loaded_asset(mat)
    manager.set_editor_property(prop,mat)
assert levels.save_current_level()
assert levels.load_level(MAIN)
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
stream=u.GameplayStatics.get_streaming_level(world,SUB)
if not stream:stream=u.EditorLevelUtils.add_level_to_world(world,SUB,u.LevelStreamingAlwaysLoaded)
assert stream
assert levels.set_current_level_by_name('Office_Demo')
assert levels.save_current_level()
report=dict(status='complete',sublevel=SUB,static_mesh_actors=len(meshes),pickable_buildings=len(shapes),
            geometry_runtime='One shared glass mesh and one outline mesh; CPU ray picking with bounds broad phase',
            existing_buildings_and_roads_modified=False,backup=str(backup))
(OUT/'campus_install_report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
u.log('CAMPUS_INSTALL_COMPLETE '+json.dumps(report))
