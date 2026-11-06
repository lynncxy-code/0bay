import bpy,json
from pathlib import Path
out=Path(__file__).resolve().parents[2]/'SourceAssets/UnitreeGo2/Generated'
mesh=bpy.data.objects['SK_Go2']
data=[]
for slot in mesh.material_slots:
    m=slot.material;n=m.node_tree.nodes.get('Principled BSDF')
    data.append({'name':m.name,'color':list(n.inputs['Base Color'].default_value),'metallic':n.inputs['Metallic'].default_value,'roughness':n.inputs['Roughness'].default_value})
(out/'materials.json').write_text(json.dumps(data,indent=2))
