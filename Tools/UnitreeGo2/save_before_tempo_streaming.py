import unreal as u,json,shutil,datetime
from pathlib import Path
root=Path(u.Paths.project_dir());out=root/'Saved/Go2'
editor=u.get_editor_subsystem(u.UnrealEditorSubsystem);world=editor.get_editor_world()
assert world.get_path_name().startswith('/Game/OfficeShell/Maps/Office_Demo_ArtMerged.')
packages=list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())+list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
backup=out/('BeforeTempoStreaming_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'));backup.mkdir(parents=True,exist_ok=True)
saved=[]
for p in packages:
    name=p.get_name();saved.append(name)
    if name.startswith('/Game/'):
        for ext in ['.umap','.uasset','.uexp','.ubulk']:
            source=root/'Content'/(name[6:]+ext)
            if source.exists():
                target=backup/source.relative_to(root/'Content');target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
actors=u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
data={'saved_packages':saved,'disk_backup':str(backup),'camera':str(editor.get_level_viewport_camera_info()),
      'actors':[{'path':a.get_path_name(),'label':a.get_actor_label(),'transform':a.get_actor_transform().export_text()} for a in actors]}
if packages:assert u.EditorLoadingAndSavingUtils.save_packages(packages,True),'Saving current work failed'
assert not list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages()),'Maps still dirty'
assert not list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()),'Assets still dirty'
data['safe_to_restart']=True
(out/'tempo_streaming_checkpoint.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
u.SystemLibrary.quit_editor()
