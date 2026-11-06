import unreal as u,time,json,traceback
from pathlib import Path
out=Path('D:/0bay/OfficeShell/Saved/SecurityIntroBuild/overview-3-10-test.json');stage=0;start=None;web=None;scanner=None;report={'samples':[]};ready=time.monotonic()
def sample(w,t):
 lines=u.GameplayStatics.get_all_actors_with_tag(w,'MatrixDeviceLink');pc=u.GameplayStatics.get_player_controller(w,0);cam=u.GameplayStatics.get_player_camera_manager(w,0)
 overview=next(a for a in u.GameplayStatics.get_all_actors_of_class(w,u.CameraActor) if a.actor_has_tag('OntoTwinViewForBusiness=bv.network-security'))
 return {'t':round(t,2),'visible':sum(not a.get_editor_property('hidden') for a in lines),'floor':scanner.get_editor_property('floor_grid_active'),'follow':scanner.get_editor_property('use_demo_camera'),'scan_count':scanner.get_editor_property('scan_count'),'camera_error_cm':(cam.get_camera_location()-overview.get_actor_location()).length()}
def tick(dt):
 global stage,start,web,scanner,report
 try:
  w=u.EditorLevelLibrary.get_game_world()
  if not w:return
  if stage==0:
   if time.monotonic()-ready<15:return
   scanner=u.GameplayStatics.get_all_actors_of_class(w,u.Go2DeviceScanner)[0]
   web=next(c for a in u.GameplayStatics.get_all_actors_of_class(w,u.Actor) for c in a.get_components_by_class(u.OntoTwinWebInteractionComponent))
   assert web.open_business_view('bv.network-security','');start=u.GameplayStatics.get_time_seconds(w);stage=1;return
  t=u.GameplayStatics.get_time_seconds(w)-start
  if stage in [1,2,3,4] and t>=[1.5,2.8,3.4,10.4][stage-1]:
   s=sample(w,t);report['samples'].append(s)
   assert not s['follow']
   if stage<=2:assert not s['floor'] and s['visible']==0
   if stage==3:assert s['floor'] and s['visible']==0
   if stage==4:assert s['floor'] and s['visible']==150
   assert s['camera_error_cm']<10,s
   stage+=1
   if stage==5:scanner.set_follow_camera(True);start=u.GameplayStatics.get_time_seconds(w)
   return
  if stage==5 and t>1.2:
   s=sample(w,t);assert s['follow'];report['manual_follow_on']=True;scanner.set_follow_camera(False);stage=6;start=u.GameplayStatics.get_time_seconds(w);return
  if stage==6 and t>1:
   assert not scanner.get_editor_property('use_demo_camera');web.back();stage=7;start=u.GameplayStatics.get_time_seconds(w);return
  if stage==7 and t>.5:
   assert sample(w,t)['visible']==0
   assert web.open_business_view('bv.network-security','');stage=8;start=u.GameplayStatics.get_time_seconds(w);return
  if stage==8 and t>1:
   web.back();stage=9;start=u.GameplayStatics.get_time_seconds(w);return
  if stage==9 and t>11:
   s=sample(w,t);assert s['visible']==0 and not s['floor'];report['early_exit_cancels_timers']=True;report['ok']=True
   out.write_text(json.dumps(report,indent=2),encoding='utf-8');u.unregister_slate_post_tick_callback(handle);u.EditorLevelLibrary.editor_end_play()
 except:
  report['error']=traceback.format_exc();out.write_text(json.dumps(report,indent=2),encoding='utf-8');u.unregister_slate_post_tick_callback(handle);u.EditorLevelLibrary.editor_end_play()
handle=u.register_slate_post_tick_callback(tick);u.get_editor_subsystem(u.LevelEditorSubsystem).editor_play_simulate()
