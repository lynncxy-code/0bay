"""Build a rigid skeletal Go2 from the unmodified official URDF and Collada assets.
Run with Blender 5.1 --background --python build_go2.py.
"""
import bpy, json, math, hashlib, xml.etree.ElementTree as ET
from pathlib import Path
from mathutils import Matrix, Vector, Euler, Quaternion

PROJECT = Path(__file__).resolve().parents[2]
SRC = PROJECT / 'SourceAssets/UnitreeGo2'
OUT = SRC / 'Generated'
OUT.mkdir(parents=True, exist_ok=True)
NS = {'c': 'http://www.collada.org/2005/11/COLLADASchema'}
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
scene = bpy.context.scene
scene.unit_settings.system = 'METRIC'
scene.unit_settings.scale_length = 1.0
scene.render.fps = 30

def origin(el):
    if el is None: return Matrix.Identity(4)
    xyz = [float(v) for v in el.get('xyz','0 0 0').split()]
    rpy = [float(v) for v in el.get('rpy','0 0 0').split()]
    return Matrix.Translation(Vector(xyz)) @ Euler(rpy,'XYZ').to_matrix().to_4x4()

urdf = ET.parse(SRC/'Official/go2_description/urdf/go2_description.urdf').getroot()
joints = {j.find('child').get('link'): j for j in urdf.findall('joint')}
frames = {'base': Matrix.Identity(4)}
def link_frame(name):
    if name not in frames:
        j=joints[name]; frames[name]=link_frame(j.find('parent').get('link')) @ origin(j.find('origin'))
    return frames[name]

links = [l for l in urdf.findall('link') if l.find('visual/geometry/mesh') is not None]
bone_names = {l.get('name'): ('base' if l.get('name')=='base' else joints[l.get('name')].get('name')) for l in links}
armdata = bpy.data.armatures.new('Go2_Official_Skeleton')
arm = bpy.data.objects.new('Go2_Rig',armdata)
scene.collection.objects.link(arm)
bpy.context.view_layer.objects.active=arm
arm.select_set(True)
bpy.ops.object.mode_set(mode='EDIT')
for l in links:
    name=l.get('name'); b=armdata.edit_bones.new(bone_names[name]); b.head=link_frame(name).translation; b.tail=b.head+Vector((0,0.04,0))
    if name!='base': b.parent=armdata.edit_bones[bone_names[joints[name].find('parent').get('link')]]
bpy.ops.object.mode_set(mode='OBJECT')
arm.show_in_front=True
arm['source']='unitreerobotics/unitree_ros@3ae893d905d11795ddf1075825ee5010364a1109'
arm['joint_angle_unit']='radian'
arm['active_joint_count']=12

material_cache={}
def material(effect, label):
    color=effect.find('.//c:diffuse/c:color',NS)
    rgba=tuple(float(v) for v in color.text.split()) if color is not None else (0.3,0.3,0.3,1)
    # Preserve official diffuse colors. Metallic/roughness are rendering choices.
    metal=1.0 if ('metal' in label.lower() or '\u91d1\u5c5e' in label or label.startswith('Material')) else 0.0
    key=(rgba,metal)
    if key not in material_cache:
        mat=bpy.data.materials.new('M_Go2_'+hashlib.sha1(repr(key).encode()).hexdigest()[:8]); mat.use_nodes=True
        bsdf=mat.node_tree.nodes.get('Principled BSDF'); bsdf.inputs['Base Color'].default_value=rgba
        bsdf.inputs['Metallic'].default_value=metal;bsdf.inputs['Roughness'].default_value=0.38 if metal else 0.65
        mat.diffuse_color=rgba;mat['official_material']=label;material_cache[key]=mat
    return material_cache[key]

mesh_objects=[]
def import_dae(path, transform, bone):
    doc=ET.parse(path).getroot()
    assert doc.find('c:asset/c:up_axis',NS).text=='Z_UP'
    assert float(doc.find('c:asset/c:unit',NS).get('meter'))==1.0
    effects={e.get('id'):e for e in doc.findall('c:library_effects/c:effect',NS)}
    materials={m.get('id'):effects[m.find('c:instance_effect',NS).get('url')[1:]] for m in doc.findall('c:library_materials/c:material',NS)}
    geometries={g.get('id'):g for g in doc.findall('c:library_geometries/c:geometry',NS)}
    def walk(node, parent):
        local=Matrix.Identity(4)
        for ch in node:
            tag=ch.tag.split('}')[-1]
            if tag=='matrix':
                v=list(map(float,ch.text.split())); local=local @ Matrix([v[i:i+4] for i in range(0,16,4)])
            elif tag=='translate':local=local @ Matrix.Translation(Vector(map(float,ch.text.split())))
            elif tag=='rotate':
                v=list(map(float,ch.text.split())); local=local @ Matrix.Rotation(math.radians(v[3]),4,Vector(v[:3]))
            elif tag=='scale':
                v=list(map(float,ch.text.split())); local=local @ Matrix.Diagonal(Vector(v+[1]))
        world=parent @ local
        for inst in node.findall('c:instance_geometry',NS):
            geo=geometries[inst.get('url')[1:]].find('c:mesh',NS)
            sources={}
            for s in geo.findall('c:source',NS):
                a=s.find('c:float_array',NS);stride=int(s.find('c:technique_common/c:accessor',NS).get('stride','1'))
                v=list(map(float,a.text.split()));sources[s.get('id')]=[v[i:i+stride] for i in range(0,len(v),stride)]
            vertices={v.get('id'):v.find("c:input[@semantic='POSITION']",NS).get('source')[1:] for v in geo.findall('c:vertices',NS)}
            binds={m.get('symbol'):m.get('target')[1:] for m in inst.findall('.//c:instance_material',NS)}
            position_source=None;faces=[];normals=[];uvs=[];mat_indices=[];slots=[]
            nmat=world.to_3x3().inverted().transposed()
            for tri in geo.findall('c:triangles',NS):
                inputs={i.get('semantic'):(int(i.get('offset')),i.get('source')[1:]) for i in tri.findall('c:input',NS)}
                stride=1+max(v[0] for v in inputs.values()); raw=list(map(int,tri.find('c:p',NS).text.split()))
                vo,vs=inputs['VERTEX']; ps=vertices[vs]
                assert position_source in (None,ps);position_source=ps
                label=binds[tri.get('material')]; mat=material(materials[label],label)
                if mat not in slots:slots.append(mat)
                for i in range(0,len(raw),3*stride):
                    face=[]
                    for k in range(3):
                        off=i+k*stride;face.append(raw[off+vo])
                        no,ns=inputs['NORMAL'];normals.append(tuple((nmat @ Vector(sources[ns][raw[off+no]])).normalized()))
                        if 'TEXCOORD' in inputs:
                            uo,us=inputs['TEXCOORD'];uvs.append(tuple(sources[us][raw[off+uo]][:2]))
                        else:uvs.append((0,0))
                    faces.append(face);mat_indices.append(slots.index(mat))
            assert faces, 'Unsupported empty geometry'
            verts=[tuple(world @ Vector(v[:3])) for v in sources[position_source]]
            mesh=bpy.data.meshes.new(bone+'_Mesh');mesh.from_pydata(verts,[],faces);mesh.update()
            for mat in slots:mesh.materials.append(mat)
            for poly,mi in zip(mesh.polygons,mat_indices):poly.material_index=mi;poly.use_smooth=True
            mesh.normals_split_custom_set(normals)
            uv=mesh.uv_layers.new(name='UVMap');uv.data.foreach_set('uv',[v for pair in uvs for v in pair])
            obj=bpy.data.objects.new(bone+'_Visual',mesh);scene.collection.objects.link(obj)
            group=obj.vertex_groups.new(name=bone);group.add(list(range(len(verts))),1.0,'REPLACE')
            obj.parent=arm;mod=obj.modifiers.new('Go2_RigidSkin','ARMATURE');mod.object=arm
            mesh_objects.append(obj)
        for child in node.findall('c:node',NS):walk(child,world)
    for n in doc.findall('c:library_visual_scenes/c:visual_scene/c:node',NS):walk(n,transform)

for l in links:
    name=l.get('name');visual=l.find('visual');filename=visual.find('geometry/mesh').get('filename').split('package://go2_description/')[1]
    import_dae(SRC/'Official/go2_description'/filename,link_frame(name) @ origin(visual.find('origin')),bone_names[name])
    print('GO2_IMPORTED',name,flush=True)

# One skeletal mesh, 17 bones: base + 12 actuated joints + 4 fixed feet.
bpy.ops.object.select_all(action='DESELECT')
for o in mesh_objects:o.select_set(True)
bpy.context.view_layer.objects.active=mesh_objects[0]
bpy.ops.object.join();mesh=bpy.context.object;mesh.name='SK_Go2'
poses=json.loads((SRC/'Official/motion_reference/stand_poses.json').read_text())
order=poses['joint_order'];up=poses['poses']['stand_up_joint_pos'];down=poses['poses']['stand_down_joint_pos']
axes={j.get('name'):Vector(map(float,j.find('axis').get('xyz').split())) for j in joints.values() if j.get('type')=='revolute'}

def pose(q):
    for name,value in zip(order,q):
        p=arm.pose.bones[name];p.rotation_mode='QUATERNION';p.rotation_quaternion=Quaternion(axes[name],value)
    arm.pose.bones['base'].location=(0,0,0);bpy.context.view_layer.update()
    feet=[(arm.matrix_world @ arm.pose.bones[leg+'_foot_joint'].matrix).translation.z for leg in ['FL','FR','RL','RR']]
    arm.pose.bones['base'].location.z=0.022-min(feet)
    bpy.context.view_layer.update()

def export(path,animated=False):
    bpy.ops.object.select_all(action='DESELECT');arm.select_set(True);mesh.select_set(True);bpy.context.view_layer.objects.active=arm
    # Rotate the whole asset before FBX conversion, not bone-local importer Euler settings.
    # With this export/import basis, URDF +X becomes Unreal +X, URDF +Y becomes Unreal -Y.
    prior_rotation=arm.rotation_euler.copy();arm.rotation_euler.z=-math.pi/2;bpy.context.view_layer.update()
    bpy.ops.export_scene.fbx(filepath=str(path),use_selection=True,object_types={'ARMATURE','MESH'},add_leaf_bones=False,
        axis_forward='-Y',axis_up='Z',apply_unit_scale=True,apply_scale_options='FBX_SCALE_NONE',use_mesh_modifiers=True,
        bake_anim=animated,bake_anim_use_all_actions=False,bake_anim_use_nla_strips=False,bake_anim_simplify_factor=0.0,
        primary_bone_axis='Y',secondary_bone_axis='X',mesh_smooth_type='FACE',path_mode='AUTO')
    arm.rotation_euler=prior_rotation;bpy.context.view_layer.update()

# Neutral bind pose is URDF q=0; animations provide valid standing/crouched configurations.
export(OUT/'SK_Go2.fbx')
clips=[('A_Go2_Idle',up,up,30),('A_Go2_Crouch',down,down,30),('A_Go2_StandUp',down,up,90),('A_Go2_StandDown',up,down,90)]
clip_report=[]
for name,start,end,n in clips:
    arm.animation_data_clear();action=bpy.data.actions.new(name);arm.animation_data_create();arm.animation_data.action=action
    scene.frame_start=1;scene.frame_end=n+1
    for i in range(n+1):
        scene.frame_set(i+1);a=math.tanh((i/n)*2.5)/math.tanh(2.5)
        pose([x+(y-x)*a for x,y in zip(start,end)])
        for p in arm.pose.bones:p.keyframe_insert('rotation_quaternion',frame=i+1,group=p.name)
        arm.pose.bones['base'].keyframe_insert('location',frame=i+1,group='base')
    action.use_fake_user=True;scene.frame_set(1);export(OUT/(name+'.fbx'),True)
    clip_report.append({'name':name,'duration_seconds':n/30,'source':'Official target poses; normalized interpolation and ground-aligned base height generated for visualization.'})
arm.animation_data_clear();pose(up)
scene.frame_start=1;scene.frame_end=91
mesh['license']='See Official/LICENSE_unitree_ros'
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'Go2_Rigged.blend'))

# Render a reference image before importing to Unreal.
world=bpy.data.worlds.new('Go2_Studio');scene.world=world;world.use_nodes=True;world.node_tree.nodes['Background'].inputs[0].default_value=(0.18,0.20,0.24,1)
bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,-0.001));floor=bpy.context.object
floor_mat=bpy.data.materials.new('StudioFloor');floor_mat.diffuse_color=(0.085,0.10,0.13,1);floor.data.materials.append(floor_mat)
for loc,power,size in [((1,-2,3),500,3),((-1,1,2),350,2)]:
    bpy.ops.object.light_add(type='AREA',location=loc);light=bpy.context.object;light.data.energy=power;light.data.shape='DISK';light.data.size=size;light.rotation_euler=(Vector((0,0,0.2))-light.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.object.camera_add(location=(1.05,-1.35,0.78));cam=bpy.context.object;cam.rotation_euler=(Vector((0.02,0,0.24))-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=1.25;scene.camera=cam
scene.render.engine='CYCLES';scene.cycles.samples=24;scene.render.resolution_x=1200;scene.render.resolution_y=900;scene.render.resolution_percentage=100;scene.render.filepath=str(OUT/'Go2_Standing.png')
bpy.ops.render.render(write_still=True)
report={'bones':list(arm.data.bones.keys()),'active_joint_order':order,'vertices':len(mesh.data.vertices),'triangles':len(mesh.data.polygons),'materials':len(mesh.data.materials),'clips':clip_report}
(OUT/'build_report.json').write_text(json.dumps(report,indent=2))
print('GO2_BUILD_COMPLETE',json.dumps(report),flush=True)
