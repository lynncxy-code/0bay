"""Idempotent minimal correction: cover the old imagery, retain road elevations."""
import json
import shutil
from datetime import datetime
from pathlib import Path
import unreal as u

PROJECT = Path(r'D:/0bay/OfficeShell')
SUB = '/Game/HaijuNorthBeta/Maps/L_HaijuNorthBeta_Exterior'
OUT = PROJECT / 'Saved/HaijuNorthBeta'
backup = OUT / ('GroundBackup_' + datetime.now().strftime('%Y%m%d_%H%M%S'))
backup.mkdir(parents=True)
shutil.copy2(PROJECT/'Content/HaijuNorthBeta/Maps/L_HaijuNorthBeta_Exterior.umap', backup)
levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
actors = u.get_editor_subsystem(u.EditorActorSubsystem)
assert levels.load_level(SUB)
all_actors = list(actors.get_all_level_actors())
before = {a.get_name(): str(a.get_actor_transform()) for a in all_actors}
ground = [a for a in all_actors if 'Ground_7' in a.get_actor_label()]
assert len(ground) == 1
a = ground[0]
mesh = a.static_mesh_component.static_mesh
local_top = mesh.get_bounding_box().max.z
location = a.get_actor_location()
location.z = 1.0 - local_top
a.set_actor_location(location, False, False)
assert levels.save_current_level()
assert levels.load_level(SUB)
after = list(actors.get_all_level_actors())
changes = [a.get_actor_label() for a in after if before[a.get_name()] != str(a.get_actor_transform())]
assert len(changes) <= 1 and all('Ground_7' in name for name in changes)
report = {'surface_z_cm': 1.0, 'actor_z_cm': location.z, 'local_mesh_z_cm': local_top,
          'modified_actors': changes, 'other_transforms_preserved': True, 'backup': str(backup),
          'scope': 'Existing entire Ground_7 mesh; roads and buildings unchanged'}
(OUT/'ground_elevation_report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
u.log('GROUND_RAISED ' + json.dumps(report))
