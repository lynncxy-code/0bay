import unreal as u,json,shutil,datetime
from pathlib import Path
root=Path(u.Paths.project_dir());out=root/'Saved/Go2'
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().startswith('/Game/OfficeShell/Maps/Office_Demo_ArtMerged.')
packages=list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())+list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
backup=out/('BeforeStreamingRestart_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'))
backup.mkdir(parents=True,exist_ok=True)
saved=[]
for p in packages:
    name=p.get_name();saved.append(name)
    if name.startswith('/Game/'):
        for ext in ['.umap','.uasset','.uexp','.ubulk']:
            source=root/'Content'/(name[6:]+ext)
            if source.exists():
                target=backup/source.relative_to(root/'Content');target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
if packages:assert u.EditorLoadingAndSavingUtils.save_packages(packages,True),'Could not save current editor changes'
assert not list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages()),'Maps still dirty; restart cancelled'
assert not list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()),'Assets still dirty; restart cancelled'
(out/'streaming_restart_checkpoint.json').write_text(json.dumps({'saved_packages':saved,'disk_backup':str(backup),'safe_to_restart':True},ensure_ascii=False,indent=2),encoding='utf-8')
u.SystemLibrary.quit_editor()
