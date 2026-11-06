"""Import only /Game/Robots/UnitreeGo2 and build an isolated preview map."""
import unreal, json, traceback, os
from pathlib import Path
PROJECT=Path(unreal.Paths.project_dir())
SOURCE=PROJECT/'SourceAssets/UnitreeGo2/Generated'
DEST='/Game/Robots/UnitreeGo2'
tools=unreal.AssetToolsHelpers.get_asset_tools()
report={'assets':[],'warnings':[]}

def run_import(name,skeleton=None):
    existing=unreal.load_asset(DEST+('/Mesh/' if skeleton is None else '/Animations/')+name) if unreal.EditorAssetLibrary.does_asset_exist(DEST+('/Mesh/' if skeleton is None else '/Animations/')+name) else None
    if existing and os.environ.get('GO2_REIMPORT')!='1' and (skeleton is not None or existing.get_editor_property('skeleton') is not None):
        report['assets'].append(existing.get_path_name());return existing
    if existing:
        old_data=existing.get_editor_property('asset_import_data')
        if old_data:
            old_data.set_editor_property('import_rotation',unreal.Rotator(0,0,0))
            unreal.EditorAssetLibrary.save_loaded_asset(existing,False)
    opts=unreal.FbxImportUI()
    opts.automated_import_should_detect_type=False
    opts.import_as_skeletal=True
    opts.import_mesh=skeleton is None
    opts.import_animations=skeleton is not None
    opts.import_materials=skeleton is None
    opts.import_textures=False
    opts.create_physics_asset=False
    opts.override_full_name=True
    opts.mesh_type_to_import=unreal.FBXImportType.FBXIT_SKELETAL_MESH if skeleton is None else unreal.FBXImportType.FBXIT_ANIMATION
    if skeleton is not None:
        opts.skeleton=skeleton
        opts.override_animation_name=name
    data=opts.skeletal_mesh_import_data if skeleton is None else opts.anim_sequence_import_data
    data.convert_scene=True;data.convert_scene_unit=True;data.force_front_x_axis=True
    # Forward correction is baked into the complete FBX object transform in build_go2.py.
    data.import_rotation=unreal.Rotator(0,0,0)
    if skeleton is None:
        data.normal_import_method=unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS
        data.set_editor_property('use_t0_as_ref_pose',False)
        data.set_editor_property('update_skeleton_reference_pose',True)
    else:
        data.set_editor_property('use_default_sample_rate',True)
        data.set_editor_property('import_bone_tracks',True)
    task=unreal.AssetImportTask()
    task.filename=str(SOURCE/(name+'.fbx'))
    task.destination_path=DEST+'/Mesh' if skeleton is None else DEST+'/Animations'
    task.destination_name=name
    task.automated=True;task.replace_existing=True;task.save=True;task.options=opts;task.factory=unreal.FbxFactory()
    task.replace_existing_settings=True
    tools.import_asset_tasks([task])
    paths=list(task.imported_object_paths)
    assert paths, 'Import produced no assets: '+name
    report['assets']+=paths
    objects=[unreal.load_asset(p) for p in paths]
    wanted=unreal.SkeletalMesh if skeleton is None else unreal.AnimSequence
    asset=next(o for o in objects if isinstance(o,wanted))
    if skeleton is None:
        sk=asset.get_editor_property('skeleton');assert sk is not None
        unreal.EditorAssetLibrary.save_loaded_asset(sk,False)
        for slot in asset.get_editor_property('materials'):
            mat=slot.get_editor_property('material_interface')
            if mat:unreal.EditorAssetLibrary.save_loaded_asset(mat,False)
    unreal.EditorAssetLibrary.save_loaded_asset(asset,False)
    return asset

world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world,'Interchange.FeatureFlags.Import.FBX 0')
mesh=run_import('SK_Go2')
skeleton=mesh.get_editor_property('skeleton')
assert skeleton is not None
animations={n:run_import(n,skeleton) for n in ['A_Go2_Idle','A_Go2_Crouch','A_Go2_StandUp','A_Go2_StandDown']}
for a in animations.values():a.set_preview_skeletal_mesh(mesh)

# A drag-and-drop actor with persistent default mesh and idle animation.
factory=unreal.BlueprintFactory();factory.set_editor_property('parent_class',unreal.SkeletalMeshActor)
bp=unreal.load_asset(DEST+'/BP_Go2') if unreal.EditorAssetLibrary.does_asset_exist(DEST+'/BP_Go2') else tools.create_asset('BP_Go2',DEST,unreal.Blueprint,factory)
assert bp
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
cdo=unreal.get_default_object(bp.generated_class())
component=cdo.get_component_by_class(unreal.SkeletalMeshComponent)
component.set_skeletal_mesh_asset(mesh)
component.override_animation_data(animations['A_Go2_Idle'],True,True,0.0,1.0)
component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
unreal.EditorAssetLibrary.save_loaded_asset(bp)
report['assets'].append(bp.get_path_name())

levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
preview_exists=unreal.EditorAssetLibrary.does_asset_exist(DEST+'/Maps/L_Go2_Preview')
assert levels.load_level(DEST+'/Maps/L_Go2_Preview') if preview_exists else levels.new_level(DEST+'/Maps/L_Go2_Preview')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
def preview_actor(label,cls,location,rotation=unreal.Rotator()):
    found=next((a for a in actors.get_all_level_actors() if a.get_actor_label()==label),None)
    if found:return found
    a=actors.spawn_actor_from_class(cls,location,rotation);a.set_actor_label(label);return a
floor=preview_actor('Go2_Preview_Floor',unreal.StaticMeshActor,unreal.Vector(0,0,-1))
floor.set_actor_label('Go2_Preview_Floor');floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Plane'))
floor.set_actor_scale3d(unreal.Vector(5,5,1))
sun=next((a for a in actors.get_all_level_actors() if isinstance(a,unreal.DirectionalLight)),None) or preview_actor('Go2_Preview_Sun',unreal.DirectionalLight,unreal.Vector(0,0,300),unreal.Rotator(-45,-30,0))
sun.light_component.set_intensity(3.0)
sky=next((a for a in actors.get_all_level_actors() if isinstance(a,unreal.SkyLight)),None) or preview_actor('Go2_Preview_Sky',unreal.SkyLight,unreal.Vector(0,0,200))
sky.light_component.set_intensity(1.0)
sky.light_component.set_editor_property('mobility',unreal.ComponentMobility.MOVABLE)
sky.light_component.set_editor_property('real_time_capture',True)
for label,offset,clip in [('Go2_Standing',-65,'A_Go2_Idle'),('Go2_Crouched',65,'A_Go2_Crouch')]:
    actor=preview_actor(label,bp.generated_class(),unreal.Vector(0,offset,0))
    actor.set_actor_label(label)
    comp=actor.get_component_by_class(unreal.SkeletalMeshComponent)
    comp.override_animation_data(animations[clip],True,True,0,1)
    comp.set_update_animation_in_editor(True)
    comp.set_position(0,False)
camera=preview_actor('Go2_Preview_Camera',unreal.CameraActor,unreal.Vector(170,-240,130),unreal.Rotator(-18,55,0))
camera.set_actor_label('Go2_Preview_Camera')
unreal.EditorLevelLibrary.set_level_viewport_camera_info(unreal.Vector(170,-240,130),unreal.Rotator(-18,55,0))
assert levels.save_current_level()
unreal.EditorAssetLibrary.save_directory(DEST,only_if_is_dirty=False,recursive=True)

# Assert the imported scale, hierarchy and animation assets are usable.
check=actors.spawn_actor_from_class(unreal.SkeletalMeshActor,unreal.Vector())
comp=check.skeletal_mesh_component;comp.set_skeletal_mesh_asset(mesh)
bones=[str(comp.get_bone_name(i)) for i in range(comp.get_num_bones())]
expected=json.loads((SOURCE/'build_report.json').read_text())['bones']
assert set(expected).issubset(bones),(expected,bones)
assert len(bones) in (17,18),bones
ref={n:{'parent':str(comp.get_parent_bone(n)),'translation_cm':list(comp.get_ref_pose_position(comp.get_bone_index(n)).to_tuple())} for n in bones}
report['bones']=ref
report['clips']={n:{'path':a.get_path_name(),'duration':a.get_play_length()} for n,a in animations.items()}
assert all(abs(report['clips'][n]['duration']-seconds)<0.05 for n,seconds in [('A_Go2_Idle',1),('A_Go2_Crouch',1),('A_Go2_StandUp',3),('A_Go2_StandDown',3)])
report['mesh']=mesh.get_path_name();report['skeleton']=skeleton.get_path_name();report['blueprint']=bp.get_path_name();report['map']=DEST+'/Maps/L_Go2_Preview'
actors.destroy_actor(check)
(SOURCE/'import_report.json').write_text(json.dumps(report,indent=2))
unreal.log('GO2_IMPORT_COMPLETE '+json.dumps(report))
