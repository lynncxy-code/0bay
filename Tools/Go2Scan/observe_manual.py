import unreal as u,json
from pathlib import Path
samples=[]; last_sample=-1; test_world=None
out=Path('D:/0bay/OfficeShell/Saved/Go2Scan/manual_validation.json')
def manual_observe(dt):
 global last_sample,test_world
 world=u.EditorLevelLibrary.get_game_world()
 if not world:return
 scanner=u.GameplayStatics.get_actor_of_class(world,u.Go2DeviceScanner)
 if not scanner:return
 t=u.GameplayStatics.get_time_seconds(world)
 if int(t)==last_sample:return
 last_sample=int(t)
 n=0
 for a in u.GameplayStatics.get_all_actors_of_class(world,u.Actor):
  for m in a.get_components_by_class(u.StaticMeshComponent):
   mat=m.get_editor_property('overlay_material')
   if mat and 'Go2DeviceScanner' in mat.get_path_name():n+=1
 samples.append({'time':round(t,1),'count':scanner.get_editor_property('scan_count'),'scanning':scanner.get_editor_property('scanning'),'auto':scanner.get_editor_property('auto_scan'),'intensity':scanner.get_editor_property('scan_intensity'),'found':scanner.get_editor_property('detected_count'),'highlighted':n})
 out.write_text(json.dumps(samples,indent=2))
manual_observer=u.register_slate_post_tick_callback(manual_observe)
