import unreal as u,json
from pathlib import Path
m=u.load_asset('/Game/ScanGridEffect/Materials/MaterialFunctions/MPC_Scan')
d={k:[{'name':str(x.get_editor_property('parameter_name')),'value':str(x.get_editor_property('default_value'))} for x in m.get_editor_property(k)] for k in ['scalar_parameters','vector_parameters']}
Path('D:/0bay/OfficeShell/Saved/Go2Scan/mpc.json').write_text(json.dumps(d,indent=2))
