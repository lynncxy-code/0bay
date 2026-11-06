import unreal as u,json
from pathlib import Path
out=Path(u.Paths.project_saved_dir())/'Go2'
editor=u.get_editor_subsystem(u.UnrealEditorSubsystem);levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
actors=u.get_editor_subsystem(u.EditorActorSubsystem);world=editor.get_editor_world()
dog=next(a for a in actors.get_all_level_actors() if isinstance(a,u.Go2RouteActor))
start=u.Vector(2500,-1300,4320);end=u.Vector(2700,-1300,4320)
hits=u.SystemLibrary.box_trace_multi(world,start+u.Vector(0,0,30),end+u.Vector(0,0,30),u.Vector(40,25,25),u.Rotator(),u.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[dog],u.DrawDebugTrace.NONE,True)
assert not hits,'Chosen route obstructed'
ground=[]
for x in [2500,2600,2700]:
    hit=u.SystemLibrary.line_trace_single(world,u.Vector(x,-1300,4340),u.Vector(x,-1300,4280),u.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[dog],u.DrawDebugTrace.NONE,True)
    assert hit
    info=hit.to_tuple();ground.append({'x':x,'impact_z':info[5].z,'actor':info[9].get_actor_label()})
    assert abs(info[5].z-4320)<2,ground
dog.modify();dog.set_actor_location(start,False,False);dog.reset_route()
assert levels.set_current_level_by_name('L_Go2_OfficeRoute')
assert levels.save_current_level()
assert levels.set_current_level_by_name('Office_Demo_ArtMerged')
camera=u.Vector(2790,-1350,4470)
editor.set_level_viewport_camera_info(camera,u.MathLibrary.find_look_at_rotation(camera,u.Vector(2580,-1300,4340)))
actors.set_selected_level_actors([dog])
report={'passed':True,'world_position_cm':start.to_tuple(),'local_actor_offset_cm':[0,100,0],
    'level_translation_cm':[2500,-1400,4320],'route_length_cm':200,'route_clearance_hits':0,'sweep_half_size_cm':[40,25,25],
    'ground_checks':ground,'saved_sublevel':'/Game/Robots/UnitreeGo2/Maps/L_Go2_OfficeRoute'}
(out/'office_route_clearance_final.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print('GO2_OFFICE_CLEAR_ROUTE_SAVED')
