import unreal as u,time,json
from pathlib import Path
if not globals().get('gallery_capture_v3',False):
 gallery_capture_v3=True
 if "cap_handle" in globals():u.unregister_slate_post_tick_callback(cap_handle)
 cap_dir=Path('D:/0bay/OfficeShell/Saved/MatrixLinks/review_frames');cap_dir.mkdir(exist_ok=True)
 cap_index=0;cap_pending=None;cap_time=time.monotonic();cap_records=[]
 def capture_gallery(dt):
  global cap_index,cap_pending,cap_time,cap_task
  now=time.monotonic()
  if cap_pending:
   if not cap_pending.exists():return
   cap_records.append({'file':str(cap_pending),'time':now});cap_pending=None;cap_index+=1;cap_time=now
  if cap_index>=28:
   (cap_dir/'records.json').write_text(json.dumps(cap_records,indent=2))
   u.unregister_slate_post_tick_callback(cap_handle)
   u.get_editor_subsystem(u.UnrealEditorSubsystem).set_level_viewport_camera_info(u.Vector(-2700,1750,550),u.Rotator())
   return
  if now-cap_time<.35:return
  if cap_index<8:
   loc=u.Vector(-2700,1750,550);prefix='styles';idx=cap_index;size=(1600,1000)
  else:
   loc=u.Vector(-3200,1700,-1840);prefix='pulses';idx=cap_index-8;size=(1600,700)
  u.get_editor_subsystem(u.UnrealEditorSubsystem).set_level_viewport_camera_info(loc,u.Rotator())
  cap_pending=cap_dir/('%s_%02d.png'%(prefix,idx))
  cap_task=u.AutomationLibrary.take_high_res_screenshot(size[0],size[1],str(cap_pending),delay=0)
 cap_handle=u.register_slate_post_tick_callback(capture_gallery)
