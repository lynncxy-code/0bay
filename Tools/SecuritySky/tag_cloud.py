import unreal as u
import shutil
from pathlib import Path
from datetime import datetime
root=Path('D:/0bay/OfficeShell')
u.get_editor_subsystem(u.LevelEditorSubsystem).load_level('/Game/OfficeShell/Maps/Office_Demo_ArtMerged')
clouds=[a for a in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors() if isinstance(a,u.VolumetricCloud)]
assert len(clouds)==1
a=clouds[0]
package=a.get_path_name().split('.')[0]
file=root/'Content'/(package.removeprefix('/Game/')+'.umap')
backup=root/'Saved/SecuritySky'/datetime.now().strftime('%Y%m%d-%H%M%S-cloud')
backup.mkdir(parents=True,exist_ok=True)
shutil.copy2(file,backup/file.name)
a.set_editor_property('tags',list(dict.fromkeys([str(t) for t in a.tags]+['OntoTwinDefaultBackground'])))
assert u.EditorLoadingAndSavingUtils.save_map(u.load_asset(package),package)
