import unreal as u
from pathlib import Path
Path('D:/0bay/OfficeShell/Saved/MatrixLinks/instant_green_requested.txt').write_text('ready')
u.SystemLibrary.execute_console_command(u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world(),'LiveCoding.Compile')
