"""Run inside the current editor: persist the Security business overview camera.

No level reload: retain the user's current CameraActor_All transform and edits.
Only the camera's owning level is saved. Runtime lookup uses cooked actor tags.
"""
import json
import shutil
from datetime import datetime
from pathlib import Path
import unreal as u

root = Path('D:/0bay/OfficeShell')
out = root / 'Saved/BusinessCameraBinding' / datetime.now().strftime('%Y%m%d-%H%M%S')
out.mkdir(parents=True, exist_ok=True)
actors = u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
tag = 'OntoTwinViewForBusiness=bv.network-security'
cameras = [a for a in actors if isinstance(a, u.CameraActor) and a.get_actor_label() == 'CameraActor_All']
assert len(cameras) == 1, 'Expected exactly one CameraActor_All'
camera = cameras[0]
assert not any(tag in [str(t) for t in a.tags] for a in actors if a != camera), 'Duplicate business binding'
lens = camera.get_component_by_class(u.CameraComponent)
record = dict(business_id='bv.network-security', label=camera.get_actor_label(),
              actor=camera.get_path_name(), transform=lens.get_world_transform().export_text(),
              fov=lens.field_of_view, aspect_ratio=lens.aspect_ratio,
              constrain_aspect_ratio=lens.constrain_aspect_ratio,
              tags_before=[str(t) for t in camera.tags])
package = camera.get_path_name().split('.')[0]
filename = root / 'Content' / (package.removeprefix('/Game/') + '.umap')
shutil.copy2(filename, out / filename.name)
camera.modify()
camera.set_editor_property('tags', list(dict.fromkeys(record['tags_before'] + [tag])))
assert u.EditorLoadingAndSavingUtils.save_map(camera.get_level().get_outer(), package)
assert tag in [str(t) for t in camera.tags]
assert lens.get_world_transform().export_text() == record['transform']
(out / 'binding.json').write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding='utf-8')
print('BUSINESS_CAMERA_SAVED', camera.get_path_name(), tag, str(out))
