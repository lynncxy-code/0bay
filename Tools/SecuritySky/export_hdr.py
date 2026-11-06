import unreal as u
t=u.AssetExportTask()
t.object=u.load_asset('/Game/HDRI_Futuristic_Technology/HDRI/HDR_FT_02')
t.filename='D:/0bay/OfficeShell/Saved/SecuritySky/source.hdr'
t.automated=True
t.prompt=False
t.replace_identical=True
t.exporter=u.TextureCubeExporterHDR()
assert u.Exporter.run_asset_export_task(t)
