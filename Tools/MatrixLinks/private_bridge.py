import unreal as u
from pathlib import Path
cmd=Path('D:/0bay/OfficeShell/Tools/MatrixLinks/private_command.py');stamp=0
try:u.unregister_slate_post_tick_callback(matrix_private_handle)
except:pass
def matrix_private_tick(dt):
 global stamp
 if cmd.exists() and cmd.stat().st_mtime!=stamp:
  stamp=cmd.stat().st_mtime
  try:exec(compile(cmd.read_text(encoding='utf-8-sig'),str(cmd),'exec'),globals())
  except:
   import traceback
   Path('D:/0bay/OfficeShell/Saved/MatrixLinks/private_error.txt').write_text(traceback.format_exc(),encoding='utf-8')
matrix_private_handle=u.register_slate_post_tick_callback(matrix_private_tick)
