import unreal,json,math
from pathlib import Path
out=Path(unreal.Paths.project_dir())/'SourceAssets/UnitreeGo2/Generated'
dest='/Game/Robots/UnitreeGo2'
tools=unreal.AssetToolsHelpers.get_asset_tools();mesh=unreal.load_asset(dest+'/Mesh/SK_Go2')
slots=list(mesh.materials)
for i,d in enumerate(json.loads((out/'materials.json').read_text())):
    path=dest+'/Materials/'+d['name']
    m=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else tools.create_asset(d['name'],dest+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
    unreal.MaterialEditingLibrary.delete_all_material_expressions(m)
    color=unreal.MaterialEditingLibrary.create_material_expression(m,unreal.MaterialExpressionConstant3Vector,-350,-50)
    color.set_editor_property('constant',unreal.LinearColor(*d['color']))
    unreal.MaterialEditingLibrary.connect_material_property(color,'',unreal.MaterialProperty.MP_BASE_COLOR)
    for key,prop in [('metallic',unreal.MaterialProperty.MP_METALLIC),('roughness',unreal.MaterialProperty.MP_ROUGHNESS)]:
        c=unreal.MaterialEditingLibrary.create_material_expression(m,unreal.MaterialExpressionConstant,-350,100 if key=='metallic' else 180)
        c.set_editor_property('r',d[key]);unreal.MaterialEditingLibrary.connect_material_property(c,'',prop)
    m.set_editor_property('used_with_skeletal_mesh',True)
    unreal.MaterialEditingLibrary.recompile_material(m)
    assert unreal.EditorAssetLibrary.save_loaded_asset(m,False)
    slots[i].material_interface=m
mesh.set_editor_property('materials',slots)
assert unreal.EditorAssetLibrary.save_loaded_asset(mesh,False)

# A studio environment only for the new preview map.
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);assert levels.load_level(dest+'/Maps/L_Go2_Preview')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for a in actors.get_all_level_actors():
    if isinstance(a,unreal.SkyLight):
        c=a.light_component;c.set_editor_property('real_time_capture',False)
        c.set_editor_property('source_type',unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
        c.set_cubemap(unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'));c.set_intensity(1.0)
assert levels.save_current_level()

options=unreal.AnimPoseEvaluationOptions();options.optional_skeletal_mesh=mesh;options.should_retarget=False;options.extract_root_motion=False
report={}
for name in ['A_Go2_Idle','A_Go2_Crouch','A_Go2_StandUp','A_Go2_StandDown']:
    anim=unreal.load_asset(dest+'/Animations/'+name)
    report[name]={}
    for t in [0,anim.get_play_length()/2,anim.get_play_length()]:
        pose=anim.get_anim_pose_at_time(t,options)
        report[name][str(t)]={}
        for bone in ['Go2_Rig','base','FL_hip_joint','FR_hip_joint','RL_hip_joint','RR_hip_joint','FL_foot_joint','FR_foot_joint','RL_foot_joint','RR_foot_joint']:
            transform=unreal.AnimPoseExtensions.get_bone_pose(pose,bone,unreal.AnimPoseSpaces.WORLD)
            report[name][str(t)][bone]={'translation':list(transform.translation.to_tuple()),'rotation_xyzw':list(transform.rotation.to_tuple()),'scale':list(transform.scale3d.to_tuple())}
(out/'pose_validation.json').write_text(json.dumps(report,indent=2))
check=actors.spawn_actor_from_class(unreal.SkeletalMeshActor,unreal.Vector())
comp=check.skeletal_mesh_component;comp.set_skeletal_mesh_asset(mesh)
def pack(t):return {'translation':list(t.translation.to_tuple()),'rotation_xyzw':list(t.rotation.to_tuple()),'scale':list(t.scale3d.to_tuple())}
ref={str(comp.get_bone_name(i)):pack(comp.get_ref_pose_transform(i)) for i in range(comp.get_num_bones())}
locals_={}
for name in ['A_Go2_Idle','A_Go2_Crouch']:
    pose=unreal.load_asset(dest+'/Animations/'+name).get_anim_pose_at_time(0,options)
    locals_[name]={n:pack(unreal.AnimPoseExtensions.get_bone_pose(pose,n,unreal.AnimPoseSpaces.LOCAL)) for n in ref}
(out/'bone_local_transforms.json').write_text(json.dumps({'reference':ref,'poses':locals_},indent=2))
actors.destroy_actor(check)
unreal.log('GO2_FINISH_COMPLETE')
