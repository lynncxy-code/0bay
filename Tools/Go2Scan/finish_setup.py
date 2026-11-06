from pathlib import Path
exec(compile(Path('D:/0bay/OfficeShell/Tools/Go2Scan/install.py').read_text(encoding='utf-8-sig'),'install.py','exec'),globals())
exec(compile(Path('D:/0bay/OfficeShell/Tools/Go2Scan/live_bridge.py').read_text(encoding='utf-8-sig'),'live_bridge.py','exec'),globals())
def wait_scan_world_final(dt):
 w=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_game_world()
 if w and u.GameplayStatics.get_time_seconds(w)>3:
  u.unregister_slate_post_tick_callback(wait_scan_final_handle)
  exec(compile(Path('D:/0bay/OfficeShell/Tools/Go2Scan/validate_runtime.py').read_text(encoding='utf-8-sig'),'validate_runtime.py','exec'),globals())
wait_scan_final_handle=u.register_slate_post_tick_callback(wait_scan_world_final)
