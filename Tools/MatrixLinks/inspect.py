import unreal as u,json
from pathlib import Path
out=Path('D:/0bay/OfficeShell/Saved/MatrixLinks')
r={'assets':[],'materials':{},'meshes':{},'devices':[]}
u.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(['/Game/RainSymbols_VFX'],True)
for path in u.EditorAssetLibrary.list_assets('/Game/RainSymbols_VFX',recursive=True):
 a=u.load_asset(path)
 r['assets'].append({'path':path,'loaded':a is not None,'class':a.get_class().get_name() if a else ''})
 if isinstance(a,u.MaterialInstanceConstant):
  r['materials'][path]={}
  for kind in ['scalar','vector']:
   names=getattr(u.MaterialEditingLibrary,'get_'+kind+'_parameter_names')(a)
   r['materials'][path][kind]={str(n):str(getattr(u.MaterialEditingLibrary,'get_material_instance_'+kind+'_parameter_value')(a,n)) for n in names}
 if isinstance(a,u.StaticMesh):r['meshes'][path]=str(a.get_bounding_box())
for a in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors():
 for m in a.get_components_by_class(u.StaticMeshComponent):
  sm=m.get_editor_property('static_mesh')
  if sm and ('OfficeDevices_20261103' in sm.get_path_name() or 'SM_MonitorCamera_Frozen' in sm.get_path_name()):
   r['devices'].append({'actor':a.get_path_name(),'label':a.get_actor_label(),'mesh':sm.get_path_name(),'pos':m.get_world_location().to_tuple(),'hidden':a.get_editor_property('hidden')})
(out/'inspection.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf-8')
exec(open('D:/0bay/OfficeShell/Tools/Go2Scan/live_bridge.py',encoding='utf-8-sig').read())
print('MATRIX_INSPECT_DONE')
