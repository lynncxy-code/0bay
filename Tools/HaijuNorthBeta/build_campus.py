"""Blender: model illustrative 500 m landscape and bake independent building pick shapes.

No satellite textures. Original OSM footprints / estimated GBA heights are retained.
Ground details are a design proposal, not surveyed campus facilities.
"""
import ast
import json
import math
import sys
from pathlib import Path
from collections import defaultdict
import bpy
from mathutils import Vector
from mathutils.geometry import delaunay_2d_cdt

PROJECT = Path(__file__).resolve().parents[2]
OUT = PROJECT/'Art/HaijuNorthBeta/Campus500'
OUT.mkdir(parents=True, exist_ok=True)
DATA = json.loads((PROJECT/'Art/HaijuNorthBeta/Source/cropped_scene.json').read_text(encoding='utf-8'))
YAW = math.radians(20.9)
ANCHOR = (144.85456288655, -53.943765929212)

def source_xy(wx, wy):
    x,y = wx-ANCHOR[0],wy-ANCHOR[1]
    return (math.cos(YAW)*x+math.sin(YAW)*y, math.sin(YAW)*x-math.cos(YAW)*y)

CENTER = source_xy(32.75,-30.8)
RADIUS = 500
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.context.scene.unit_settings.system='METRIC'
bpy.context.scene.unit_settings.scale_length=1

def inside(x,y,ring):
    hit=False
    for a,b in zip(ring,ring[1:]+ring[:1]):
        if (a[1]>y)!=(b[1]>y) and x<(b[0]-a[0])*(y-a[1])/(b[1]-a[1])+a[0]: hit=not hit
    return hit

def triangles(rings):
    vs=[];es=[]
    for r in rings:
        n=len(vs);vs.extend(Vector(p) for p in r)
        es.extend((n+i,n+(i+1)%len(r)) for i in range(len(r)))
    vv,_,ff,*_=delaunay_2d_cdt(vs,es,[],0,1e-5)
    good=[]
    for f in ff:
        x=sum(vv[i].x for i in f)/len(f);y=sum(vv[i].y for i in f)/len(f)
        if inside(x,y,rings[0]) and not any(inside(x,y,r) for r in rings[1:]):good.append(f)
    return vv,good

def expanded(r,amount):
    # Offset adjacent edge lines; clamp acute mitres to avoid spikes.
    area=sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(r,r[1:]+r[:1]))
    result=[]
    for i,p in enumerate(r):
        a=(Vector(p)-Vector(r[i-1])).normalized();b=(Vector(r[(i+1)%len(r)])-Vector(p)).normalized()
        sign=1 if area>0 else -1
        na=Vector((a.y,-a.x))*sign;nb=Vector((b.y,-b.x))*sign
        bis=(na+nb).normalized();den=max(.3,bis.dot(na))
        result.append(tuple(Vector(p)+bis*amount/den))
    return result

shapes=[]
def pick_shape(key, rings, height, z0=0, expansion=.26):
    rings=[expanded(r, expansion if i==0 else -expansion) for i,r in enumerate(rings)]
    vs=[];ts=[];edges=[]
    def vert(p,z):return [round(p[0]*100,3),round(-p[1]*100,3),round(z*100,3)]
    def face(points):
        n=len(vs);vs.extend(points)
        for i in range(1,len(points)-1):ts.extend([n,n+i,n+i+1])
    for r in rings:
        for a,b in zip(r,r[1:]+r[:1]):
            face([vert(a,z0),vert(b,z0),vert(b,height),vert(a,height)])
            edges.extend([vert(a,z0),vert(b,z0),vert(a,height),vert(b,height),vert(a,z0),vert(a,height)])
    vv,ff=triangles(rings)
    for f in ff:face([vert(vv[i],height) for i in f])
    shapes.append(dict(key=key,vertices=vs,triangles=ts,edges=edges))

for b in DATA['buildings']:pick_shape(b['key'],b['rings'],b['height']+.08)

# Two original CAD wings form one main-building target. Convert through its actual UE placement.
module=ast.parse((PROJECT.parent/'output/GrandNeoBay_Game/scripts/build_building.py').read_text(encoding='utf-8'))
cad=next(ast.literal_eval(n.value) for n in module.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='CAD' for t in n.targets))
main_rings=[]
for sign in (-1,1):
    r=[]
    for x,y in cad:
        lx=sign*(30.8-y/1000);ly=-(x/1000-32.75)
        r.append(source_xy(32.75-ly,-30.8+lx))
    main_rings.append(r)
    pick_shape('GrandNeoBay', [r], 63.4,4.5,.55)
first=shapes[-2];last=shapes.pop();offset=len(first['vertices'])
first['vertices'].extend(last['vertices']);first['triangles'].extend(t+offset for t in last['triangles']);first['edges'].extend(last['edges'])
sys.path.insert(0,str(Path(__file__).resolve().parent))
from landmark_contract import merge_shapes
shapes=merge_shapes(shapes)
(OUT/'building_shapes.json').write_text(json.dumps(shapes,separators=(',',':')),encoding='utf-8')

# Spatial occupancy for new decorative objects, in source metres.
building_rings=[b['rings'][0] for b in DATA['buildings']]+main_rings
grid=defaultdict(list)
for r in building_rings:
    for gx in range(math.floor(min(p[0] for p in r)/40),math.floor(max(p[0] for p in r)/40)+1):
        for gy in range(math.floor(min(p[1] for p in r)/40),math.floor(max(p[1] for p in r)/40)+1):grid[gx,gy].append(r)
segments=[]
for road in DATA['roads']:
    for a,b in zip(road['points'],road['points'][1:]):
        if math.dist(a,b)>.1:segments.append((Vector(a),Vector(b),road['width']))
def distance_segment(p,a,b):
    ab=b-a;t=max(0,min(1,(p-a).dot(ab)/ab.length_squared));return (p-a-ab*t).length
def free(x,y,road_margin=1):
    if math.dist((x,y),CENTER)>RADIUS:return False
    if any(inside(x,y,r) for r in grid[math.floor(x/40),math.floor(y/40)]):return False
    p=Vector((x,y))
    return all(distance_segment(p,a,b)>w/2+road_margin for a,b,w in segments)

colors={'Paving':(.41,.46,.46),'Stone':(.57,.61,.59),'Asphalt':(.065,.085,.105),
        'Paint':(.79,.83,.78),'Grass':(.13,.25,.20),'Plant':(.105,.205,.16),
        'Canopy':(.18,.32,.23),'Bark':(.15,.13,.10),'Metal':(.085,.13,.16),'Timber':(.30,.21,.12)}
colors.update(Ground=(.34,.52,.68),RoadExtension=(.075,.10,.12),WalkExtension=(.61,.62,.59))
batches=defaultdict(lambda:[[],[]])
def face(group,points):
    vs,fs=batches[group];n=len(vs);vs.extend(points);fs.append(tuple(range(n,n+len(points))))
def rectangle(c,u,sx,sy):
    v=Vector((-u.y,u.x));return [tuple(c+u*x*sx/2+v*y*sy/2) for x,y in [(-1,-1),(1,-1),(1,1),(-1,1)]]
def slab(group,ring,z,h=0):
    face(group,[(x,y,z+h) for x,y in ring])
    if h:
        for a,b in zip(ring,ring[1:]+ring[:1]):face(group,[(*a,z),(*b,z),(*b,z+h),(*a,z+h)])
def box(group,c,u,sx,sy,z,h):slab(group,rectangle(c,u,sx,sy),z,h)
def line(group,a,b,width,z):
    a,b=Vector(a),Vector(b);d=b-a
    if d.length>.001:slab(group,rectangle((a+b)/2,d.normalized(),d.length,width),z)
def tree(c,k):
    h=5.5+(k%4)*.35
    box('Bark',c,Vector((1,0)),.25,.25,.15,h*.58)
    rings=[(h*.40,1.4),(h*.66,2.35),(h*.88,1.35),(h,0)]
    for (z,r),(zz,rr) in zip(rings,rings[1:]):
        for i in range(8):
            a=i*math.tau/8;b=(i+1)*math.tau/8
            face('Canopy',[(c.x+r*math.cos(a),c.y+r*math.sin(a),z),(c.x+r*math.cos(b),c.y+r*math.sin(b),z),
                           (c.x+rr*math.cos(b),c.y+rr*math.sin(b),zz),(c.x+rr*math.cos(a),c.y+rr*math.sin(a),zz)])

def below(poly,limit):
    result=[]
    for a,b in zip(poly,poly[1:]+poly[:1]):
        ia,ib=a[1]<=limit,b[1]<=limit
        if ia:result.append(a)
        if ia!=ib:
            t=(limit-a[1])/(b[1]-a[1]);result.append((a[0]+t*(b[0]-a[0]),limit))
    return result

# Original northern context ends at source Y=-261.616 m. Continue the same modeled
# ground to a 540 m radius, with an exact shared edge rather than overlapping planes.
circle=[(CENTER[0]+540*math.cos(i*math.tau/128),CENTER[1]+540*math.sin(i*math.tau/128)) for i in range(128)]
extension=below(circle,-261.61599609375)
slab('Ground',extension,.01)

# Continue source road polylines beyond the northern crop. This is geometry only.
road_polys=[];walk_polys=[]
for a,b,w in segments:
    d=b-a;rel=a-Vector(CENTER)
    aa=d.length_squared;bb=2*rel.dot(d);cc=rel.length_squared-530**2
    disc=bb*bb-4*aa*cc
    if disc<0:continue
    lo=max(0,(-bb-math.sqrt(disc))/(2*aa));hi=min(1,(-bb+math.sqrt(disc))/(2*aa))
    if hi<=lo:continue
    p,q=a+d*lo,a+d*hi
    for width,collection in ((w,road_polys),(w+3,walk_polys)):
        poly=below(rectangle((p+q)/2,d.normalized(),(q-p).length,width),-220)
        if len(poly)>=3:collection.append(poly)

def union_surface(polys,subtract,group,z):
    points=[];faces=[]
    for r in polys+subtract:
        n=len(points);points.extend(Vector(p) for p in r);faces.append(tuple(range(n,n+len(r))))
    if not faces:return
    vv,_,ff,*_=delaunay_2d_cdt(points,[],faces,1,.0001)
    for f in ff:
        x=sum(vv[i].x for i in f)/len(f);y=sum(vv[i].y for i in f)/len(f)
        if any(inside(x,y,r) for r in polys) and not any(inside(x,y,r) for r in subtract):
            pts=[(vv[i].x,vv[i].y,z) for i in f]
            if (Vector(pts[1])-Vector(pts[0])).cross(Vector(pts[2])-Vector(pts[0])).z<0:pts.reverse()
            face(group,pts)
union_surface(road_polys,[],'RoadExtension',.055)
union_surface(walk_polys,road_polys,'WalkExtension',.025)

# Flat, flush entrance plaza: no raised building pedestal. Existing roads render above it.
center=Vector(CENTER);u=Vector((math.sin(YAW),-math.cos(YAW)))
slab('Paving',rectangle(center,u,142,128),.016)
v=Vector((-u.y,u.x))
for t in range(-70,71,7):line('Stone',center+u*t-v*64,center+u*t+v*64,.06,.021)
for t in range(-63,64,7):line('Stone',center+v*t-u*71,center+v*t+u*71,.06,.021)

counts=defaultdict(int);occupied=[]
for si,(a,b,w) in enumerate(segments):
    if w<7:continue
    d=b-a;l=d.length;u=d.normalized();v=Vector((-u.y,u.x))
    for t in range(22,int(l),46):
        base=a+u*t
        for side in (-1,1):
            c=base+v*(w/2+9)*side
            ring=rectangle(c,u,22,10)
            probes=ring+[tuple(c),tuple(c+u*11),tuple(c-u*11)]
            if not all(free(x,y,1.4) for x,y in probes):continue
            if any((c-p).length<24 for p in occupied):continue
            occupied.append(c)
            box('Stone',c,u,22,10,.019,.12)
            if (si+t+side)%3==0:
                # Seven bays plus a driving aisle within each small parking pocket.
                box('Asphalt',c,u,21.4,9.4,.141,.005)
                for n in range(9):
                    p=c+u*(-10+n*2.5)-v*4.35
                    line('Paint',p,p+v*5,.10,.151)
                line('Paint',c-u*10+v*.65,c+u*10+v*.65,.10,.151)
                counts['parking_bays']+=8
            else:
                box('Grass',c,u,20.8,8.8,.141,.025)
                for off in (-7,0,7):tree(c+u*off,si+off)
                box('Plant',c+v*3.7,u,18,.7,.17,.55)
                counts['garden_islands']+=1;counts['new_trees']+=3
            counts['pocket_plazas']+=1
    # Lights, paired timber benches and bollards on clear roadside paving.
    for t in range(18,int(l),54):
        for side in (-1,1):
            c=a+u*t+v*(w/2+2.1)*side
            if not free(c.x,c.y,1.7):continue
            if not all(free(*(c+u*q),1.6) for q in (-1.6,1.6)):continue
            box('Stone',c,u,4,1.4,.017,.13)
            box('Metal',c-u*1.55,u,.13,.13,.15,5.2)
            box('Metal',c-u*1.55+v*.45,u,.18,1.15,5.2,.12)
            box('Paint',c-u*1.55+v*.45,u,.12,.8,5.19,.02)
            box('Timber',c+u*.5,u,1.8,.46,.48,.12)
            for q in (-.2,1.2):box('Metal',c+u*q,u,.08,.4,.15,.33)
            box('Timber',c+u*.5+v*.22,u,1.8,.08,.65,.34)
            counts['benches']+=1;counts['lights']+=1

# Open-space stepping paths and planting beds, avoiding roads and existing footprints.
for ix in range(-10,11):
    for iy in range(-10,11):
        c=center+Vector((ix*44,iy*44));u=Vector((1,0))
        if (ix+iy)%3 or any((c-p).length<30 for p in occupied):continue
        if not all(free(c.x+x,c.y+y,5) for x in (-13,0,13) for y in (-9,0,9)):continue
        box('Stone',c,u,26,18,.018,.1)
        box('Grass',c,u,24.8,16.8,.118,.025)
        box('Paving',c,u,26,2.8,.145,.008)
        for x in (-8,8):
            for y in (-5,5):tree(c+Vector((x,y)),ix+iy)
        occupied.append(c);counts['garden_islands']+=1;counts['new_trees']+=4

objects=[]
for name,(verts,faces) in batches.items():
    m=bpy.data.materials.new('Campus_'+name);m.use_nodes=True
    bs=m.node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=(*colors[name],1);bs.inputs['Roughness'].default_value=.85
    mesh=bpy.data.meshes.new('Campus_'+name);mesh.from_pydata(verts,[],faces);mesh.update();mesh.materials.append(m)
    obj=bpy.data.objects.new(mesh.name,mesh);bpy.context.collection.objects.link(obj);objects.append(obj)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.export_scene.gltf(filepath=str(OUT/'Campus500.glb'),export_format='GLB',use_selection=True,export_yup=True)
report=dict(radius_m=RADIUS,center_source_m=list(CENTER),style='Illustrative modeled landscape; no satellite texture',
            pickable_buildings=len(shapes),ground_meshes=len(objects),detail_counts=dict(counts),
            vertices=sum(len(v) for v,f in batches.values()),source_geometry_unchanged=True)
(OUT/'generation_report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('CAMPUS_GENERATED',json.dumps(report))
