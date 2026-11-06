import unreal as u,time,json,traceback
from pathlib import Path
out=Path('D:/0bay/OfficeShell/Saved/SecurityIntroBuild/reentry-cleanup-test.json')
report={'cycles':[],'exits':[]};ready=time.monotonic();stage='ready';cycle=0;checkpoint=0;start=0;baseline=0

def sample(w):
 cam=u.GameplayStatics.get_player_camera_manager(w,0);overview=next(a for a in u.GameplayStatics.get_all_actors_of_class(w,u.CameraActor) if a.actor_has_tag('OntoTwinViewForBusiness=bv.network-security'))
 return {'time':round(u.GameplayStatics.get_time_seconds(w)-start,2),'floor':scanner.get_editor_property('floor_grid_active'),'scanning':scanner.get_editor_property('scanning'),'count':scanner.get_editor_property('scan_count'),'follow':scanner.get_editor_property('use_demo_camera'),'grid':scanner.get_editor_property('scan_grid').is_active(),'post_process':scanner.get_editor_property('scan_post_process').get_editor_property('blend_weight'),'lines':sum(not a.get_editor_property('hidden') for a in u.GameplayStatics.get_all_actors_with_tag(w,'MatrixDeviceLink')),'camera_error':(cam.get_camera_location()-overview.get_actor_location()).length()}
def begin(w):
 global start,checkpoint,baseline
 baseline=scanner.get_editor_property('scan_count');assert web.open_business_view('bv.network-security','');start=u.GameplayStatics.get_time_seconds(w);checkpoint=0

def tick(dt):
 global stage,cycle,checkpoint,start,baseline,scanner,web
 try:
  w=u.EditorLevelLibrary.get_game_world()
  if not w:return
  if stage=='ready':
   if time.monotonic()-ready<15:return
   report['scene_line_count']=len(u.GameplayStatics.get_all_actors_with_tag(w,'MatrixDeviceLink'))
   scanner=u.GameplayStatics.get_all_actors_of_class(w,u.Go2DeviceScanner)[0];web=next(c for a in u.GameplayStatics.get_all_actors_of_class(w,u.Actor) for c in a.get_components_by_class(u.OntoTwinWebInteractionComponent));begin(w);stage='cycle';return
  t=u.GameplayStatics.get_time_seconds(w)-start
  if stage=='cycle' and t>=[1.5,2.8,3.4,10.4][checkpoint]:
   s=sample(w);report['cycles'].append(dict(cycle=cycle+1,**s));assert s['camera_error']<10 and not s['follow'],s
   if checkpoint<2:assert not s['floor'] and not s['grid'] and s['lines']==0 and s['count']==baseline,s
   elif checkpoint==2:assert s['floor'] and s['grid'] and s['lines']==0 and s['count']==baseline+1,s
   else:assert s['floor'] and s['grid'] and s['lines']==report['scene_line_count'] and s['count']==baseline+1,s
   checkpoint+=1
   if checkpoint==4:web.back();start=u.GameplayStatics.get_time_seconds(w);stage='exit'
   return
  if stage=='exit' and t>.6:
   s=sample(w);report['exits'].append(s);assert not s['floor'] and not s['scanning'] and not s['grid'] and s['post_process']==0 and s['lines']==0,s
   cycle+=1
   if cycle<2:begin(w);stage='cycle'
   else:begin(w);stage='early'
   return
  if stage=='early' and t>1:web.back();start=u.GameplayStatics.get_time_seconds(w);stage='wait';return
  if stage=='wait' and t>11:
   s=sample(w);assert not s['floor'] and not s['grid'] and s['lines']==0 and s['count']==baseline,s
   report['early_exit']=s;report['ok']=True;finish()
 except:
  report['error']=traceback.format_exc();finish()
def finish():
 out.write_text(json.dumps(report,indent=2),encoding='utf-8');u.unregister_slate_post_tick_callback(handle);u.EditorLevelLibrary.editor_end_play()
handle=u.register_slate_post_tick_callback(tick);u.get_editor_subsystem(u.LevelEditorSubsystem).editor_play_simulate()
