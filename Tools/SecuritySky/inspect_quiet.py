import unreal as u
from pathlib import Path
import json
mesh=u.load_asset('/Engine/EngineSky/SM_SkySphere')
mi=u.load_asset('/Game/OfficeShell/Materials/MI_SecurityOverview_Example11')
r=dict(slots=[str(x) for x in mesh.static_materials],parent=str(mi.parent),params={})
for p in ['YawDegrees','PitchDegrees','Brightness','Saturation','HighlightLimit']:
    r['params'][p]=u.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(mi,p)
Path('D:/0bay/OfficeShell/Saved/SecuritySky/quiet-inspection.json').write_text(json.dumps(r,indent=2))
