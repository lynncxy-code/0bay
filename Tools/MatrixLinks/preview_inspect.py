import unreal as u
from pathlib import Path
exec(open('D:/0bay/OfficeShell/Tools/Go2Scan/live_bridge.py',encoding='utf-8-sig').read())
t=u.AssetExportTask();t.object=u.load_asset('/Game/RainSymbols_VFX/Maps/Demo');t.filename='D:/0bay/OfficeShell/Saved/MatrixLinks/demo.t3d';t.exporter=u.LevelExporterT3D();t.automated=True;t.prompt=False
print(u.Exporter.run_asset_export_task(t))
