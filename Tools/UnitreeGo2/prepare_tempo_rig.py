import unreal as u,json
from pathlib import Path
src='/TempoQuadruped/Animations/SKM_Unitree_Go2_CtrlRig';dst='/Game/Robots/UnitreeGo2/Tempo/CR_Go2_Tempo'
u.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(['/TempoQuadruped','/Game/Robots/UnitreeGo2'],True)
rig=u.load_asset(dst) if u.EditorAssetLibrary.does_asset_exist(dst) else u.AssetToolsHelpers.get_asset_tools().duplicate_asset('CR_Go2_Tempo','/Game/Robots/UnitreeGo2/Tempo',u.load_asset(src))
assert rig
controller=rig.get_controller()
assert controller.set_pin_default_value('Locomotor_1.Debug.bDrawDebug','False')
assert controller.set_pin_default_value('Locomotor_1.Pelvis.PelvisBone','(Type=Bone,Name="body")')
assert controller.set_pin_default_value('Locomotor_1.Pelvis.BobOffset','-1.0')
assert controller.set_pin_default_value('Locomotor_1.Movement.Deceleration','80.0')
rig.recompile_vm()
u.BlueprintEditorLibrary.compile_blueprint(rig)
assert u.EditorAssetLibrary.save_loaded_asset(rig,False)
(Path(u.Paths.project_dir())/'SourceAssets/UnitreeGo2/Generated/tempo_rig_provenance.json').write_text(json.dumps({'source':src,'local_copy':dst,'changes':['Disable upstream debug drawing; route overlay supplied by test course.','Bind Locomotor pelvis to body; reduce bob offset from -8 cm to -1 cm.','Increase Locomotor deceleration from 30 to 80 cm/s^2 for stop tests.'],'gait_solver':'Upstream Locomotor_1 plus PBIK, not generated walking clips'},indent=2))
print('TEMPO_LOCAL_RIG_PREPARED')
