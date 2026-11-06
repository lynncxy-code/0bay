import unreal as u
import json
from pathlib import Path
u.get_editor_subsystem(u.LevelEditorSubsystem).load_level('/Game/OfficeShell/Maps/Office_Demo_ArtMerged')
rows=[]
for a in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors():
    rows.append(dict(label=a.get_actor_label(),name=a.get_name(),cls=a.get_class().get_path_name(),
        path=a.get_path_name(),hidden=a.get_editor_property('hidden'),tags=[str(t) for t in a.tags]))
Path('D:/0bay/OfficeShell/Saved/SecuritySky/roof-actors.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
