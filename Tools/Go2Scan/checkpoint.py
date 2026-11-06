import unreal as u,json,shutil,datetime
from pathlib import Path
root=Path(u.Paths.project_dir());out=root/'Saved/Go2Scan'
m=u.load_asset('/Game/ScanGridEffect/Materials/MaterialFunctions/MPC_Scan')
d={k:[{'name':str(x.get_editor_property('parameter_name')),'value':str(x.get_editor_property('default_value'))} for x in m.get_editor_property(k)] for k in ['scalar_parameters','vector_parameters']}
(out/'mpc.json').write_text(json.dumps(d,indent=2))
backup=out/('Backup_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'));backup.mkdir(parents=True,exist_ok=True)
packages=list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())+list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
for p in packages:
 name=p.get_name()
 if name.startswith('/Game/'):
  for ext in ['.umap','.uasset']:
   src=root/'Content'/(name[6:]+ext)
   if src.exists():
    dst=backup/src.relative_to(root/'Content');dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
if packages: assert u.EditorLoadingAndSavingUtils.save_packages(packages,True)
# Snapshot the saved current levels too: preserve user placements before adding the scanner.
for rel in ['OfficeShell/Maps/Office_Demo_ArtMerged.umap','Robots/UnitreeGo2/Maps/L_Go2_TempoOffice.umap']:
 src=root/'Content'/rel;dst=backup/'SavedCurrent'/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
(out/'checkpoint.json').write_text(json.dumps({'backup':str(backup),'saved':[p.get_name() for p in packages]},indent=2))
u.SystemLibrary.quit_editor()
