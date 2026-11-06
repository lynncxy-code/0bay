"""Persist camera-to-instance tags for runtime/cooked lookup, using authored labels once."""
import json
import shutil
from pathlib import Path
import unreal as u

root = Path('D:/0bay/OfficeShell')
out = root / 'Saved/CameraViewBinding'
out.mkdir(parents=True, exist_ok=True)
levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert levels.load_level('/Game/OfficeShell/Maps/Office_Demo_ArtMerged')
actors = u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
bindings = []
selected = []
for n in range(17, 31):
    matches = [a for a in actors if isinstance(a, u.CameraActor) and a.get_actor_label() == 'OntoTwin_Camera_' + str(n)]
    assert len(matches) == 1, (n, len(matches))
    camera = matches[0]
    package = camera.get_path_name().split('.')[0]
    filename = root / 'Content' / (package.removeprefix('/Game/') + '.umap')
    backup = out / 'maps-before' / filename.relative_to(root / 'Content')
    backup.parent.mkdir(parents=True, exist_ok=True)
    if not backup.exists(): shutil.copy2(filename, backup)
    selected.append((camera, n))
    c = camera.get_component_by_class(u.CameraComponent)
    bindings.append(dict(instance_id=str(n), actor=camera.get_path_name(), label=camera.get_actor_label(),
        camera_transform=c.get_world_transform().export_text(), fov=c.field_of_view,
        aspect_ratio=c.aspect_ratio, constrain_aspect_ratio=c.constrain_aspect_ratio, original_tags=[str(t) for t in camera.tags]))
for camera, n in selected:
    tags = [str(t) for t in camera.tags if not str(t).startswith('OntoTwinViewForInstance=')]
    tags.append('OntoTwinViewForInstance=' + str(n))
    camera.modify()
    camera.set_editor_property('tags', tags)
assert levels.save_all_dirty_levels()
assert levels.load_level('/Game/OfficeShell/Maps/Office_Demo_ArtMerged')
actors = u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
for n in range(17, 31):
    assert len([a for a in actors if isinstance(a, u.CameraActor) and 'OntoTwinViewForInstance=' + str(n) in [str(t) for t in a.tags]]) == 1
(out / 'bindings.json').write_text(json.dumps(bindings, ensure_ascii=False, indent=2), encoding='utf-8')
print('CAMERA_VIEW_BINDINGS_SAVED: 14 unique authored cameras; transforms unchanged')
