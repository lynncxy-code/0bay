"""Compare Unreal evaluated animation poses with independent URDF forward kinematics."""
import json,math,xml.etree.ElementTree as ET
from pathlib import Path
import numpy as np
SRC=Path(__file__).resolve().parents[2]/'SourceAssets/UnitreeGo2'
OUT=SRC/'Generated'
urdf=ET.parse(SRC/'Official/go2_description/urdf/go2_description.urdf').getroot()
joints={j.find('child').get('link'):j for j in urdf.findall('joint')}
poses=json.loads((SRC/'Official/motion_reference/stand_poses.json').read_text())
up=np.array(poses['poses']['stand_up_joint_pos']);down=np.array(poses['poses']['stand_down_joint_pos'])
order=poses['joint_order']
def rotation(axis,angle):
    x,y,z=axis;c=math.cos(angle);s=math.sin(angle);v=1-c
    return np.array([[x*x*v+c,x*y*v-z*s,x*z*v+y*s],[y*x*v+z*s,y*y*v+c,y*z*v-x*s],[z*x*v-y*s,z*y*v+x*s,z*z*v+c]])
def frame(el):
    m=np.eye(4)
    if el is not None:
        m[:3,3]=list(map(float,el.get('xyz','0 0 0').split()))
        r,p,y=map(float,el.get('rpy','0 0 0').split())
        m[:3,:3]=rotation([0,0,1],y)@rotation([0,1,0],p)@rotation([1,0,0],r)
    return m
def fk(q):
    cache={'base':np.eye(4)};values=dict(zip(order,q))
    def get(link):
        if link not in cache:
            j=joints[link];m=get(j.find('parent').get('link'))@frame(j.find('origin'))
            if j.get('name') in values:
                rot=np.eye(4);rot[:3,:3]=rotation(list(map(float,j.find('axis').get('xyz').split())),values[j.get('name')]);m=m@rot
            cache[link]=m
        return cache[link]
    for leg in ['FL','FR','RL','RR']:get(leg+'_foot')
    height=0.022-min(cache[leg+'_foot'][2,3] for leg in ['FL','FR','RL','RR'])
    return {('base' if k=='base' else joints[k].get('name')):(v[:3,3]+[0,0,height])*[100,-100,100] for k,v in cache.items()}
observed=json.loads((OUT/'pose_validation.json').read_text());errors=[]
for name,samples in observed.items():
    for t,bones in samples.items():
        if name.endswith('Idle'):q=up
        elif name.endswith('Crouch'):q=down
        else:
            a=math.tanh(float(t)/3*2.5)/math.tanh(2.5)
            q=down+(up-down)*a if name.endswith('StandUp') else up+(down-up)*a
        expected=fk(q)
        for bone,d in bones.items():
            if bone in expected:errors.append(float(np.linalg.norm(np.array(d['translation'])-expected[bone])))
assert max(errors)<0.001,{'max_error_cm':max(errors),'axis_or_scale_mismatch':True}

def mul(a,b):
    av=np.array(a[:3]);bv=np.array(b[:3]);return np.r_[a[3]*bv+b[3]*av+np.cross(av,bv),a[3]*b[3]-np.dot(av,bv)]
local=json.loads((OUT/'bone_local_transforms.json').read_text());bindings=[];angle_errors=[]
for i,name in enumerate(order):
    ref=np.array(local['reference'][name]['rotation_xyzw']);ref/=np.linalg.norm(ref)
    live=np.array(local['poses']['A_Go2_Idle'][name]['rotation_xyzw']);live/=np.linalg.norm(live)
    delta=mul(np.r_[-ref[:3],ref[3]],live)
    if delta[3]<0:delta=-delta
    axis=delta[:3]/math.sin(up[i]/2);axis/=np.linalg.norm(axis)
    for clip,q in [('A_Go2_Idle',up),('A_Go2_Crouch',down)]:
        pred=mul(ref,np.r_[axis*math.sin(q[i]/2),math.cos(q[i]/2)])
        actual=np.array(local['poses'][clip][name]['rotation_xyzw']);actual/=np.linalg.norm(actual)
        err=2*math.acos(min(1.0,abs(float(np.dot(pred,actual)))))
        angle_errors.append(err)
    bindings.append({'sdk_index':i,'bone':name,'reference_rotation_xyzw':ref.tolist(),'joint_axis_in_imported_bone_local':axis.tolist(),'angle_unit':'radian'})
assert max(angle_errors)<0.0001,max(angle_errors)
mapping={'source':'Computed from and validated against imported UE reference/animation transforms','position_urdf_m_to_unreal_cm':[100,-100,100],'quaternion_formula':'local_rotation = reference_rotation * axis_angle(joint_axis_in_imported_bone_local, q_radians)','note':'Preserve reference translations/scales and the Go2_Rig conversion bone. In telemetry mode set base translation to its reference pose; do not add the baked animation stance height.','joints':bindings}
(OUT/'ue_joint_bindings.json').write_text(json.dumps(mapping,indent=2))
report={'position_checks':len(errors),'max_position_error_cm':max(errors),'joint_rotation_checks':len(angle_errors),'max_rotation_error_radians':max(angle_errors),'forward_axis':'Unreal +X','up_axis':'Unreal +Z'}
(OUT/'validation_summary.json').write_text(json.dumps(report,indent=2));print(json.dumps(report))
