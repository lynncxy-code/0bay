import unreal,json,copy
from pathlib import Path
PROJECT=Path(unreal.Paths.project_dir());ROOT='/Game/Robots/UnitreeGo2'
out=PROJECT/'SourceAssets/UnitreeGo2/Generated'
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
cls=unreal.load_class(None,'/Script/OfficeHost.Go2TelemetryActor');assert cls
factory=unreal.BlueprintFactory();factory.set_editor_property('parent_class',cls)
bp=unreal.load_asset(ROOT+'/BP_Go2Live') if unreal.EditorAssetLibrary.does_asset_exist(ROOT+'/BP_Go2Live') else unreal.AssetToolsHelpers.get_asset_tools().create_asset('BP_Go2Live',ROOT,unreal.Blueprint,factory)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(bp,False)
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
live_map=ROOT+'/Maps/L_Go2_Live'
new=not unreal.EditorAssetLibrary.does_asset_exist(live_map)
if new:assert unreal.EditorAssetLibrary.duplicate_asset(ROOT+'/Maps/L_Go2_Preview',live_map)
assert levels.load_level(live_map)
if new:
    for a in actors.get_all_level_actors():
        if a.get_actor_label() in ['Go2_Standing','Go2_Crouched']:actors.destroy_actor(a)
live=next((a for a in actors.get_all_level_actors() if a.get_actor_label()=='Go2_Live_ReadOnly'),None)
if live is None:
    live=actors.spawn_actor_from_class(bp.generated_class(),unreal.Vector());live.set_actor_label('Go2_Live_ReadOnly')
live.set_editor_property('allow_synthetic_data',False)
live.set_editor_property('expected_robot_ip','192.168.123.18')
assert levels.save_current_level()

# Test the native runtime path in a separate transient actor, leaving the live actor's defaults intact.
test=actors.spawn_actor_from_class(cls,unreal.Vector(300,0,0));test.set_editor_property('receive_enabled',False)
test.set_editor_property('smoothing_seconds',0.0)
data=json.loads((PROJECT/'SourceAssets/UnitreeGo2/Official/motion_reference/stand_poses.json').read_text())
packet={'schema':'go2.telemetry.v1','source':'synthetic','robot_ip':'192.168.123.18','session':'native-test','seq':1,'sample_age_ms':0,
        'q':data['poses']['stand_up_joint_pos'],'imu_quat_wxyz':[1,0,0,0],'mode':-1,'gait_type':-1}
assert not test.apply_telemetry_json(json.dumps(packet)), 'Synthetic data must be rejected by default'
test.set_editor_property('allow_synthetic_data',True)
assert test.apply_telemetry_json(json.dumps(packet))
assert not test.get_editor_property('live_data_fresh')
assert not test.apply_telemetry_json(json.dumps(packet)), 'Duplicate sequence accepted'
reports=[]
for index,key in enumerate(['stand_up_joint_pos','stand_down_joint_pos'],2):
    packet['seq']=index;packet['q']=data['poses'][key]
    assert test.apply_telemetry_json(json.dumps(packet))
    mesh=test.get_editor_property('robot_mesh');offset=mesh.get_editor_property('relative_location')
    feet=[]
    for leg in ['FL','FR','RL','RR']:
        p=mesh.get_bone_location_by_name(leg+'_foot_joint',unreal.BoneSpaces.COMPONENT_SPACE)
        feet.append(p.z+offset.z)
    assert max(abs(z-2.2) for z in feet)<0.001,feet
    assert max(abs(a-b) for a,b in zip(test.get_editor_property('joint_angles_radians'),packet['q']))<1e-5
    reports.append({'pose':key,'foot_centers_z_cm':feet,'body_offset_cm':offset.z})
for change in [{'seq':0},{'sample_age_ms':2000},{'robot_ip':'192.168.123.19'},{'q':[0]*11},{'q':[100]*12},{'imu_quat_wxyz':[0,0,0,0]},{'q':[float('nan')]*12}]:
    bad=copy.deepcopy(packet);bad['seq']=100;bad.update(change)
    assert not test.apply_telemetry_json(json.dumps(bad)),change
assert test.get_editor_property('accepted_frames')==3
actors.destroy_actor(test)
(out/'live_native_validation.json').write_text(json.dumps({'passed':True,'runtime_class':cls.get_path_name(),'blueprint':bp.get_path_name(),'map':live_map,'synthetic_rejected_by_default':True,'invalid_frame_cases':7,'poses':reports},indent=2))
unreal.log('GO2_LIVE_ASSETS_AND_NATIVE_TEST_PASSED')
