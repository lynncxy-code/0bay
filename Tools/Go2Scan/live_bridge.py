import unreal as u
from pathlib import Path
scan_command=Path('D:/0bay/OfficeShell/Tools/Go2Scan/live_command.py')
scan_command_stamp=scan_command.stat().st_mtime if scan_command.exists() else 0
def scan_command_tick(dt):
 global scan_command_stamp
 if scan_command.exists() and scan_command.stat().st_mtime!=scan_command_stamp:
  scan_command_stamp=scan_command.stat().st_mtime
  try:exec(compile(scan_command.read_text(encoding='utf-8-sig'),str(scan_command),'exec'),globals())
  except Exception:
   import traceback
   Path('D:/0bay/OfficeShell/Saved/Go2Scan/live_error.txt').write_text(traceback.format_exc())
scan_command_handle=u.register_slate_post_tick_callback(scan_command_tick)
