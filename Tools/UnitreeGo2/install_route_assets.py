"""Create the reusable offline actor and a test map; does not define the user's three final sequences."""
import unreal, json, math
from pathlib import Path
ROOT='/Game/Robots/UnitreeGo2'
OUT=Path(unreal.Paths.project_dir())/'SourceAssets/UnitreeGo2/Generated'
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
cls=unreal.load_class(None,'/Script/OfficeHost.Go2RouteActor');assert cls
factory=unreal.BlueprintFactory();factory.set_editor_property('parent_class',cls)
bp=unreal.load_asset(ROOT+'/BP_Go2Route') if unreal.EditorAssetLibrary.does_asset_exist(ROOT+'/BP_Go2Route') else unreal.AssetToolsHelpers.get_asset_tools().create_asset('BP_Go2Route',ROOT,unreal.Blueprint,factory)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(bp,False)
level=ROOT+'/Maps/L_Go2_RoutePreview'
new=not unreal.EditorAssetLibrary.does_asset_exist(level)
if new:assert unreal.EditorAssetLibrary.duplicate_asset(ROOT+'/Maps/L_Go2_Preview',level)
assert levels.load_level(level)
if new:
    for a in actors.get_all_level_actors():
        if a.get_actor_label() in ['Go2_Standing','Go2_Crouched']:actors.destroy_actor(a)
demo=next((a for a in actors.get_all_level_actors() if a.get_actor_label()=='Go2_Route_Test_NotFinalPreset'),None)
if not demo:
    demo=actors.spawn_actor_from_class(bp.generated_class(),unreal.Vector(-100,0,0))
    demo.set_actor_label('Go2_Route_Test_NotFinalPreset')
demo.reset_route()
assert levels.save_current_level()

foot_names=[leg+'_foot_joint' for leg in ['FR','FL','RR','RL']]
expected=json.loads((OUT.parent/'Official/motion_reference/stand_poses.json').read_text())['poses']
def positions(mesh):
    return [mesh.get_bone_location_by_name(n,unreal.BoneSpaces.WORLD_SPACE) for n in foot_names]
def dist(a,b):return math.sqrt(sum((x-y)**2 for x,y in zip(a.to_tuple(),b.to_tuple())))
def simulate(points,speed=35,dt=1/60,closed=False,loop=False):
    a=actors.spawn_actor_from_class(cls,unreal.Vector(0,0,0));a.set_editor_property('auto_start',False)
    spline=a.get_editor_property('route');spline.set_spline_points([unreal.Vector(*p) for p in points],unreal.SplineCoordinateSpace.LOCAL,True)
    spline.set_closed_loop(closed,True)
    a.set_editor_property('speed_cm_per_second',speed);a.set_editor_property('loop',loop)
    assert a.start_route(),a.get_editor_property('status')
    mesh=a.get_editor_property('robot_mesh');states=[];max_fk=0.;max_stance_slide=0.;min_foot=1e9;max_frame_motion=0.
    prev=None;previous_swing=None;previous_targets=None;pause_checked=False
    for frame in range(int(90/dt)):
        a.advance_simulation(dt)
        state=str(a.get_editor_property('state')).split('.')[-1].split(':')[0].rstrip('>')
        if not states or states[-1]!=state:states.append(state)
        feet=positions(mesh);targets=[unreal.Vector(*p.to_tuple()) for p in a.get_editor_property('foot_targets_world')];swings=list(a.get_editor_property('feet_in_swing'))
        min_foot=min(min_foot,min(p.z for p in feet))
        if prev is not None:max_frame_motion=max(max_frame_motion,max(dist(x,y) for x,y in zip(feet,prev)))
        if state in ['WALKING','SETTLING_FEET']:
            max_fk=max(max_fk,max(dist(x,y) for x,y in zip(feet,targets)))
            if previous_targets is not None:
                for i in range(4):
                    if not swings[i] and not previous_swing[i] and dist(targets[i],previous_targets[i])<1e-6:
                        max_stance_slide=max(max_stance_slide,dist(feet[i],prev[i]))
            if not pause_checked:
                before=a.get_editor_property('distance_cm');pose=list(a.get_editor_property('joint_angles_radians'))
                a.toggle_pause();a.advance_simulation(.5)
                assert a.get_editor_property('distance_cm')==before and list(a.get_editor_property('joint_angles_radians'))==pose
                a.toggle_pause();pause_checked=True
        prev=feet;previous_targets=targets;previous_swing=swings
        if state=='COMPLETE':break
    else:raise AssertionError({'message':'Sequence did not complete','states':states,'distance':a.get_editor_property('distance_cm'),'status':a.get_editor_property('status')})
    q=list(a.get_editor_property('joint_angles_radians'))
    assert max(abs(x-y) for x,y in zip(q,expected['stand_down_joint_pos']))<1e-5
    assert abs(a.get_editor_property('distance_cm')-spline.get_spline_length())<.001
    assert min_foot>=2.19,(min_foot,states)
    assert max_fk<.01,{'fk_error_cm':max_fk,'reported':a.get_editor_property('max_foot_error_cm')}
    assert max_stance_slide<.005,max_stance_slide
    assert states==['STANDING_UP','WALKING','SETTLING_FEET','STANDING_PAUSE','LYING_DOWN','COMPLETE'],states
    if loop:
        a.advance_simulation(1);a.advance_simulation(1);a.advance_simulation(.1)
        assert 'STANDING_UP' in str(a.get_editor_property('state'))
    report={'speed_cm_s':speed,'dt':dt,'closed':closed,'loop':loop,'states':states,'duration_s':(frame+1)*dt,
            'length_cm':spline.get_spline_length(),'max_actual_foot_to_target_error_cm':max_fk,'max_stance_slide_cm':max_stance_slide,
            'minimum_foot_center_z_cm':min_foot,'max_foot_motion_per_frame_cm':max_frame_motion,'pause_resume_passed':pause_checked}
    a.reset_route();assert a.get_editor_property('distance_cm')==0 and not a.get_editor_property('running')
    actors.destroy_actor(a);return report

reports=[]
reports.append(simulate([(0,0,0),(200,0,0)]))
reports.append(simulate([(0,0,0),(100,0,0),(200,100,0),(200,200,0)],dt=1/30))
reports.append(simulate([(0,0,0),(200,0,0)],speed=50))
reports.append(simulate([(120,0,0),(0,120,0),(-120,0,0),(0,-120,0)],closed=True,loop=True))
a=actors.spawn_actor_from_class(cls,unreal.Vector())
a.set_editor_property('loop',True);assert not a.start_route()
a.set_editor_property('loop',False);a.get_editor_property('route').set_location_at_spline_point(1,unreal.Vector(200,0,20),unreal.SplineCoordinateSpace.LOCAL,True)
assert not a.start_route();actors.destroy_actor(a)
(OUT/'route_validation.json').write_text(json.dumps({'passed':True,'blueprint':bp.get_path_name(),'map':level,
    'scope':'Reusable offline flat-floor sequence. The three final user routes remain unspecified.',
    'tempo_installed':False,'generated_gait':'Analytic 12-joint foot IK with world-space stance contacts; visualization only, not Unitree recorded motion.',
    'cases':reports,'invalid_open_loop_and_nonplanar_routes_rejected':True},indent=2))
unreal.log('GO2_ROUTE_VALIDATION_PASSED')
