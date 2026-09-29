"""Build a small CAD-derived east-wing proxy; run with Blender --background --python.

Input outline is the ten-point simplified CAD envelope also used by the existing
GrandNeoBay source model. FBX converts Blender Y to UE -Y. Height is normalized
to one metre so the experience actor can expose floor height independently.
"""
import ast
import json
from pathlib import Path
import bpy
from mathutils import Vector
from mathutils.geometry import tessellate_polygon

PROJECT = Path(__file__).resolve().parents[2]
SOURCE = Path('D:/0bay/output/GrandNeoBay_Game/scripts/build_building.py')
OUT = PROJECT / 'Art/GrandNeoBay/Floor09'
OUT.mkdir(parents=True, exist_ok=True)
module = ast.parse(SOURCE.read_text(encoding='utf-8'))
cad = next(ast.literal_eval(n.value) for n in module.body
           if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'CAD' for t in n.targets))
poly = [(30.8 - y / 1000, x / 1000 - 32.75) for x, y in cad]
center = Vector((20.43, 0))
# Small expansion keeps the cyan skin in front of the opaque imported facade.
poly = [tuple(center + (Vector(p) - center) * 1.012) for p in poly]
if sum(a[0]*b[1] - b[0]*a[1] for a, b in zip(poly, poly[1:]+poly[:1])) < 0:
    poly.reverse()
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.context.scene.unit_settings.system = 'METRIC'
bpy.context.scene.unit_settings.scale_length = 1

def mesh(name, verts, faces):
    data = bpy.data.meshes.new(name)
    data.from_pydata(verts, [], faces)
    data.update()
    uv = data.uv_layers.new(name='PlanarUV')
    for p in data.polygons:
        normal_axis = max(range(3), key=lambda i: abs(p.normal[i]))
        axes = [i for i in range(3) if i != normal_axis]
        for loop in p.loop_indices:
            co = data.vertices[data.loops[loop].vertex_index].co
            uv.data[loop].uv = (co[axes[0]], co[axes[1]])
    ob = bpy.data.objects.new(name, data)
    bpy.context.collection.objects.link(ob)
    return ob

n = len(poly)
vertices = [(x, y, z) for z in (0, 1) for x, y in poly]
index = {(round(x, 6), round(y, 6)): i for i, (x, y) in enumerate(poly)}
triangles = [[v if isinstance(v, int) else index[(round(v.x, 6), round(v.y, 6))] for v in tri]
             for tri in tessellate_polygon([[Vector((x, y, 0)) for x, y in poly]])]
faces = [tuple(reversed(t)) for t in triangles] + [tuple(i+n for i in t) for t in triangles]
faces += [(i, (i+1)%n, (i+1)%n+n, i+n) for i in range(n)]
volume = mesh('SM_GNB_Floor09_Volume', vertices, faces)

segments = []
for a, b in zip(poly, poly[1:]+poly[:1]):
    for z in (0, 1):
        segments.append(((a[0], a[1], z), (b[0], b[1], z), False))
    segments.append(((a[0], a[1], 0), (a[0], a[1], 1), True))
vs, fs = [], []
for start, end, vertical in segments:
    a, b = Vector(start), Vector(end)
    if vertical:
        u, v = Vector((.035, 0, 0)), Vector((0, .035, 0))
    else:
        direction = (b-a).normalized()
        u, v = Vector((-direction.y, direction.x, 0))*.035, Vector((0, 0, .0073))
    k = len(vs)
    vs.extend(tuple(p+s*u+t*v) for p in (a,b) for s,t in [(-1,-1),(1,-1),(1,1),(-1,1)])
    fs.extend(tuple(k+i for i in f) for f in [(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)])
outline = mesh('SM_GNB_Floor09_Outline', vs, fs)
for ob in (volume, outline):
    bpy.ops.object.select_all(action='DESELECT')
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.fbx(filepath=str(OUT / (ob.name+'.fbx')), use_selection=True,
                             object_types={'MESH'}, axis_forward='-Y', axis_up='Z',
                             apply_unit_scale=True, bake_anim=False, add_leaf_bones=False)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'Floor09Proxy.blend'))
report = dict(source_cad_outline_mm=cad, building_wing='East', unit='metres', normalized_height=1,
              floor_target_world_z_cm=4320, volume_triangles=sum(len(p.vertices)-2 for p in volume.data.polygons),
              outline_triangles=sum(len(p.vertices)-2 for p in outline.data.polygons),
              ue_outline_cm=[[x*100,-y*100] for x,y in poly])
(OUT/'proxy_report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print('GNB_PROXY_COMPLETE', report['volume_triangles'], report['outline_triangles'])
