import unreal as u,json,time
from pathlib import Path
world=u.EditorLevelLibrary.get_game_world()
scanner=u.GameplayStatics.get_actor_of_class(world,u.Go2DeviceScanner)
assert scanner
out=Path(u.Paths.project_dir()).resolve()/'Saved/Go2Scan'
records=[]
for a in u.GameplayStatics.get_all_actors_of_class(world,u.Actor):
 for m in a.get_components_by_class(u.StaticMeshComponent):
  sm=m.get_editor_property('static_mesh')
  if sm and ('OfficeDevices_20261103' in sm.get_path_name() or 'SM_MonitorCamera_Frozen' in sm.get_path_name()):
   records.append({'actor':a.get_name(),'label':a.get_actor_label(),'hidden':a.get_editor_property('hidden'),'visible':m.is_visible(),'hidden_game':m.get_editor_property('hidden_in_game'),'overlay':str(m.get_editor_property('overlay_material')),'pos':m.get_world_location().to_tuple()})
(out/'runtime_devices.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
scanner.set_editor_property('auto_scan',False)
scanner.start_scan()
start=u.GameplayStatics.get_time_seconds(world);last=-1;samples=[]
def go2_scan_check(dt):
 global last
 t=u.GameplayStatics.get_time_seconds(world)-start
 if int(t*2)!=last:
  last=int(t*2)
  n=0
  for a in u.GameplayStatics.get_all_actors_of_class(world,u.Actor):
   for m in a.get_components_by_class(u.StaticMeshComponent):
    overlay=m.get_editor_property('overlay_material')
    if overlay and 'Go2DeviceScanner' in overlay.get_path_name():n+=1
  samples.append({'time':round(t,2),'scanning':scanner.get_editor_property('scanning'),'radius':scanner.get_editor_property('current_radius'),'detected':scanner.get_editor_property('detected_count'),'highlighted_meshes':n})
 if t>13:
  report={'samples':samples,'scan_finished':not scanner.get_editor_property('scanning'),'found_devices':max(x['detected'] for x in samples)>0,'highlight_applied':max(x['highlighted_meshes'] for x in samples)>0,'highlight_restored':samples[-1]['highlighted_meshes']==0}
  (out/'runtime_validation.json').write_text(json.dumps(report,indent=2))
  scanner.set_editor_property('auto_scan',False);scanner.start_scan()
  u.unregister_slate_post_tick_callback(go2_scan_check_handle)
go2_scan_check_handle=u.register_slate_post_tick_callback(go2_scan_check)

