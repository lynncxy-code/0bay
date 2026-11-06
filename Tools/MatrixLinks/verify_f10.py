import unreal as u,time,json
from pathlib import Path
f10_start=time.monotonic()
f10_requested=False

def f10_verify(dt):
 global f10_requested
 if time.monotonic()-f10_start<12:return
 s=u.get_editor_subsystem(u.LevelEditorSubsystem)
 if not f10_requested:
  f10_requested=True
  s.editor_play_simulate()
  return
 world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_game_world()
 if not world:return
 managers=u.GameplayStatics.get_all_actors_of_class(world,u.TwinSceneManager)
 if not managers:return
 result=[{'actor':m.get_path_name(),'enabled':m.get_editor_property('enable_runtime_editor')} for m in managers]
 Path('D:/0bay/OfficeShell/Saved/MatrixLinks/f10-verified.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
 s.editor_request_end_play()
 u.unregister_slate_post_tick_callback(f10_handle)
f10_handle=u.register_slate_post_tick_callback(f10_verify)
