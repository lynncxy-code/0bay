"""Read the existing floor datum without changing its placement or stale test baseline."""
import json
import hashlib
from pathlib import Path
import unreal as u

P=Path(r'D:/0bay/OfficeShell')
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert levels.load_level('/Game/OfficeShell/Maps/Office_Demo')
a=next(a for a in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors() if a.get_class().get_name()=='GrandNeoBayExperience')
def vec(v):return [v.x,v.y,v.z]
current=P/'Content/OfficeShell/Maps/L_GrandNeoBay_Building.umap'
backup=P/'Saved/HaijuNorthBeta/LandmarkBackup_20260923_105226/L_GrandNeoBay_Building.umap'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
report=dict(actor_location_cm=vec(a.get_actor_location()),floor_offset_cm=vec(a.get_editor_property('floor_offset')),
            actual_floor_root_world_cm=vec(a.get_editor_property('floor_root').get_world_location()),
            legacy_test_expected_floor_z_cm=4320,main_building_map_sha256=sha(current),baseline_backup_sha256=sha(backup))
report['main_building_map_unchanged_in_this_task']=report['main_building_map_sha256']==report['baseline_backup_sha256']
assert report['main_building_map_unchanged_in_this_task']
(P/'Art/HaijuNorthBeta/Acceptance/Landmarks/Floor_Datum_Context.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
u.log('FLOOR_DATUM_CONTEXT '+json.dumps(report))
