import unreal as u
from pathlib import Path
for name,path in [('ScanState','/Game/ScanGridEffect/Materials/MaterialFunctions/ScanState'),('ScanComponent','/Game/ScanGridEffect/Blueprints/BC_ScanEnvironment01')]:
 t=u.AssetExportTask();t.object=u.load_asset(path);t.filename='D:/0bay/OfficeShell/Saved/Go2Scan/'+name+'.t3d';t.automated=True;t.prompt=False;t.exporter=u.ObjectExporterT3D();u.Exporter.run_asset_export_task(t)
