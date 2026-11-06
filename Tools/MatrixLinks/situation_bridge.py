import unreal as u
from pathlib import Path
situation_command=Path('D:/0bay/OfficeShell/Tools/MatrixLinks/situation_command.py')
situation_stamp=0
def situation_tick(dt):
 global situation_stamp
 if situation_command.exists() and situation_command.stat().st_mtime!=situation_stamp:
  situation_stamp=situation_command.stat().st_mtime
  try:exec(compile(situation_command.read_text(encoding='utf-8-sig'),str(situation_command),'exec'),globals())
  except:
   import traceback
   Path('D:/0bay/OfficeShell/Saved/MatrixLinks/situation_error.txt').write_text(traceback.format_exc())
situation_handle=u.register_slate_post_tick_callback(situation_tick)
