import unreal as u
import json
from pathlib import Path
r = {}
for path in ['/HDRIBackdrop/Materials/MI_HDRI_Sky', '/HDRIBackdrop/Materials/MI_HDRI_Floor']:
    m = u.load_asset(path)
    r[path] = {}
    for kind in ['scalar', 'vector', 'texture']:
        names = getattr(u.MaterialEditingLibrary, 'get_' + kind + '_parameter_names')(m)
        r[path][kind] = {str(n): str(getattr(u.MaterialEditingLibrary, 'get_material_instance_' + kind + '_parameter_value')(m, n)) for n in names}
u.get_editor_subsystem(u.LevelEditorSubsystem).load_level('/Game/HDRI_Futuristic_Technology/Example_Levels/Example_Map_11')
a = next(a for a in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors() if a.get_actor_label() == 'HDRIBackdrop')
for p in ['ProjectionCenter', 'UseCameraProjection', 'LightingDistanceFactor', 'Size']:
    try: r[p] = str(a.get_editor_property(p))
    except Exception as e: r[p] = str(e)
Path('D:/0bay/OfficeShell/Saved/SecuritySky/material.json').write_text(json.dumps(r, indent=2))
