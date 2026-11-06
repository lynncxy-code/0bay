import unreal as u,json,math
from pathlib import Path
P=Path(u.Paths.project_dir());OUT=P/'SourceAssets/UnitreeGo2/Generated'
ROOT='/Game/Robots/UnitreeGo2';MAP=ROOT+'/Maps/L_Go2_TempoPreview'
levels=u.get_editor_subsystem(u.LevelEditorSubsystem);actors=u.get_editor_subsystem(u.EditorActorSubsystem)
lib=u.EditorAssetLibrary
cls=u.load_class(None,'/Script/OfficeHost.TempoGo2Course');assert cls
factory=u.BlueprintFactory();factory.set_editor_property('parent_class',cls)
bp=u.load_asset(ROOT+'/Tempo/BP_Go2TempoCourse') if lib.does_asset_exist(ROOT+'/Tempo/BP_Go2TempoCourse') else u.AssetToolsHelpers.get_asset_tools().create_asset('BP_Go2TempoCourse',ROOT+'/Tempo',u.Blueprint,factory)
u.BlueprintEditorLibrary.compile_blueprint(bp);assert lib.save_loaded_asset(bp,False)
new=not lib.does_asset_exist(MAP)
if new:assert lib.duplicate_asset(ROOT+'/Maps/L_Go2_RoutePreview',MAP)
assert levels.load_level(MAP)
# Keep Unreal's default floating sphere pawn out of this robot-only preview.
gm_path=ROOT+'/Tempo/BP_TempoPreviewGameMode'
gm_factory=u.BlueprintFactory();gm_factory.set_editor_property('parent_class',u.GameModeBase)
gm=u.load_asset(gm_path) if lib.does_asset_exist(gm_path) else u.AssetToolsHelpers.get_asset_tools().create_asset('BP_TempoPreviewGameMode',ROOT+'/Tempo',u.Blueprint,gm_factory)
u.get_default_object(gm.generated_class()).set_editor_property('default_pawn_class',None)
u.BlueprintEditorLibrary.compile_blueprint(gm);assert lib.save_loaded_asset(gm,False)
u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world().get_world_settings().set_editor_property('default_game_mode',gm.generated_class())
for a in actors.get_all_level_actors():
    if isinstance(a,u.Go2RouteActor):actors.destroy_actor(a)
course=next((a for a in actors.get_all_level_actors() if isinstance(a,u.TempoGo2Course)),None)
if course is None:course=actors.spawn_actor_from_class(bp.generated_class(),u.Vector())
course.set_actor_label('Go2_Tempo_ComplexCourse')
course.set_editor_property('use_preview_camera',True)
course.set_editor_property('repeat',True)
floor=next(a for a in actors.get_all_level_actors() if a.get_actor_label()=='Go2_Preview_Floor')
floor.set_actor_location(u.Vector(60,180,-1),False,False);floor.set_actor_scale3d(u.Vector(14,14,1))
camera=next(a for a in actors.get_all_level_actors() if isinstance(a,u.CameraActor))
camera.set_actor_location(u.Vector(850,-900,950),False,False)
camera.set_actor_rotation(u.MathLibrary.find_look_at_rotation(camera.get_actor_location(),u.Vector(60,170,15)),False)
camera.camera_component.set_field_of_view(48)
camera.set_editor_property('auto_activate_for_player',u.AutoReceiveInput.PLAYER0)
course.reset_course();assert levels.save_current_level()
course.set_editor_property('repeat',False)
assert course.start_course(),course.get_editor_property('state')
mesh=course.get_editor_property('robot_mesh')
frames=[]
for i in range(3000):
    course.advance_course(1/60)
    if i%15==0:
        origin,extent,radius=u.SystemLibrary.get_component_bounds(mesh)
        body=course.get_editor_property('body_world_position')
        assert all(abs(getattr(body,k)-getattr(origin,k))<=getattr(extent,k) for k in ['x','y','z']),'Animated body escaped render bounds'
        frames.append({'t':(i+1)/60,'state':course.get_editor_property('state'),'section':course.get_editor_property('current_leg'),
          'distance':course.get_editor_property('distance_cm'),'goal_speed':course.get_editor_property('target_speed_cm'),
          'actual_speed':course.get_editor_property('actual_body_speed_cm'),'body':course.get_editor_property('body_world_position').to_tuple(),
          'feet':[v.to_tuple() for v in course.get_editor_property('feet_world')],
          'hip_fr_rotation':mesh.get_socket_transform('hip_fr',u.RelativeTransformSpace.RTS_COMPONENT).rotation.to_tuple(),
          'yaw':course.get_editor_property('target_yaw_degrees')})
    if not course.get_editor_property('running'):break
result={'state':course.get_editor_property('state'),'stops':course.get_editor_property('completed_stops'),'solve_count':course.get_editor_property('rig_solve_count'),
        'route_cm':course.get_editor_property('route').get_spline_length(),'rig_class':str(course.get_editor_property('procedural_rig').get_editor_property('control_rig_class')),
        'frames':frames}
(OUT/'tempo_course_probe.json').write_text(json.dumps(result,indent=2))
assert result['state']=='Complete' and result['stops']==4,result['state']
assert all(math.isfinite(v) for f in frames for v in f['body'])
assert max(f['body'][0] for f in frames)-min(f['body'][0] for f in frames)>500
assert max(f['body'][1] for f in frames)-min(f['body'][1] for f in frames)>300
assert min(f['body'][2] for f in frames)>25 and max(f['body'][2] for f in frames)<38
assert max(f['goal_speed'] for f in frames)>=99
assert len({round(f['hip_fr_rotation'][0],3) for f in frames})>10
assert len({f['section'] for f in frames if f['state']=='Holding' and f['actual_speed']<2})==4
course.start_course()
course.advance_course(.5)
course.toggle_pause()
paused_distance=course.get_editor_property('distance_cm')
paused_solves=course.get_editor_property('rig_solve_count')
course.advance_course(.5)
assert course.get_editor_property('distance_cm')==paused_distance
assert course.get_editor_property('rig_solve_count')==paused_solves
course.toggle_pause();course.advance_course(.5)
assert course.get_editor_property('distance_cm')>paused_distance
course.reset_course()
assert course.get_editor_property('state')=='Ready'
print('TEMPO_COURSE_PROBE_COMPLETE',result['state'],result['stops'])
