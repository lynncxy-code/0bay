import unreal,json
from pathlib import Path
root='/Game/Robots/UnitreeGo2'
mesh=unreal.load_asset(root+'/Mesh/SK_Go2')
assert mesh and mesh.skeleton
mats=[s.material_interface for s in mesh.materials]
assert len(mats)==7 and all(mats)
assert all(unreal.EditorAssetLibrary.does_asset_exist(m.get_path_name()) for m in mats)
bp=unreal.load_asset(root+'/BP_Go2');assert bp
cdo=unreal.get_default_object(bp.generated_class());comp=cdo.get_component_by_class(unreal.SkeletalMeshComponent)
assert comp.get_skeletal_mesh_asset()==mesh
assert comp.get_num_bones()==18
data=comp.get_editor_property('animation_data');assert data.anim_to_play==unreal.load_asset(root+'/Animations/A_Go2_Idle')
clips={}
for name,duration in [('A_Go2_Idle',1),('A_Go2_Crouch',1),('A_Go2_StandUp',3),('A_Go2_StandDown',3)]:
    a=unreal.load_asset(root+'/Animations/'+name);assert a and a.get_editor_property('skeleton')==mesh.skeleton;assert abs(a.get_play_length()-duration)<0.05
    clips[name]=a.get_play_length()
assert unreal.EditorAssetLibrary.does_asset_exist(root+'/Maps/L_Go2_Preview')
out=Path(unreal.Paths.project_dir())/'SourceAssets/UnitreeGo2/Generated/asset_reload_validation.json'
out.write_text(json.dumps({'passed':True,'skeletal_mesh':mesh.get_path_name(),'material_count':len(mats),'bone_count':comp.get_num_bones(),'blueprint_default_animation':data.anim_to_play.get_path_name(),'clips_seconds':clips},indent=2))
unreal.log('GO2_PERSISTENCE_CHECK_PASSED')
