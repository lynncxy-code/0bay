import unreal, json
from pathlib import Path
out=Path(unreal.Paths.project_dir())/'SourceAssets/UnitreeGo2/Generated'
out.mkdir(parents=True,exist_ok=True)
names=['FbxImportUI','FbxSkeletalMeshImportData','FbxAnimSequenceImportData','AnimSequence','Skeleton','SkeletalMeshComponent','EditorLevelLibrary','SkeletalMeshEditorSubsystem','AnimationDataController']
report={n:{'doc':getattr(unreal,n).__doc__,'methods':[m for m in dir(getattr(unreal,n)) if not m.startswith('_')]} for n in names if hasattr(unreal,n)}
(out/'unreal_api_probe.json').write_text(json.dumps(report,indent=2))
unreal.log('GO2_API_PROBE_COMPLETE')
