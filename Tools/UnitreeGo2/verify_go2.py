import unreal,json
from pathlib import Path
out=Path(unreal.Paths.project_dir())/'SourceAssets/UnitreeGo2/Generated'
dest='/Game/Robots/UnitreeGo2'
mesh=unreal.load_asset(dest+'/Mesh/SK_Go2')
report={'materials':[str(s.material_interface) for s in mesh.materials], 'skeleton':str(mesh.skeleton)}
report['docs']={n:{'doc':getattr(unreal,n).__doc__,'methods':[m for m in dir(getattr(unreal,n)) if not m.startswith('_')]} for n in ['AnimPose','AnimPoseExtensions','AnimPoseEvaluationOptions','AnimPoseSpaces','AutomationLibrary'] if hasattr(unreal,n)}
report['pose_method_doc']=unreal.AnimSequence.get_anim_pose_at_time.__doc__
report['pose_bone_method_doc']=unreal.AnimPoseExtensions.get_bone_pose.__doc__
report['screenshot_doc']=unreal.AutomationLibrary.take_high_res_screenshot.__doc__
report['ref_doc']=unreal.SkeletalMeshComponent.get_ref_pose_transform.__doc__
report['refpos_doc']=unreal.SkeletalMeshComponent.get_ref_pose_position.__doc__
(out/'verification_probe.json').write_text(json.dumps(report,indent=2))
