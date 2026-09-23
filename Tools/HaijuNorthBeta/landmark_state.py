"""Read-only UE state capture shared by landmark installation and verification."""
import hashlib
import json
import unreal as u

def vec(v): return [round(v.x,6),round(v.y,6),round(v.z,6)]
def shape_data(shape):
    return dict(key=shape.get_editor_property('key'),
                vertices=[vec(p) for p in shape.get_editor_property('vertices')],
                triangles=list(shape.get_editor_property('triangles')),
                edges=[vec(p) for p in shape.get_editor_property('edges')])
def digest(value):
    return hashlib.sha256(json.dumps(value,sort_keys=True,separators=(',',':')).encode()).hexdigest()
def capture():
    result={}
    for a in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors():
        r=a.get_actor_rotation()
        record=dict(label=a.get_actor_label(),cls=a.get_class().get_path_name(),
                    location=vec(a.get_actor_location()),rotation=[round(r.pitch,6),round(r.yaw,6),round(r.roll,6)],
                    scale=vec(a.get_actor_scale3d()),hidden=a.get_editor_property('hidden'))
        if isinstance(a,u.StaticMeshActor):
            c=a.static_mesh_component;m=c.static_mesh
            record.update(mesh=m.get_path_name() if m else None,
                          materials=[c.get_material(i).get_path_name() if c.get_material(i) else None for i in range(c.get_num_materials())],
                          visible=c.get_editor_property('visible'),collision=str(c.get_collision_enabled()),
                          profile=str(c.get_collision_profile_name()),shadow=c.get_editor_property('cast_shadow'),
                          navigation=c.get_editor_property('can_ever_affect_navigation'),
                          overlap=c.get_editor_property('generate_overlap_events'),
                          distance_field=c.get_editor_property('affect_distance_field_lighting'))
        if a.get_class().get_name()=='CampusInteraction':
            record['shapes']={s.get_editor_property('key'):digest(shape_data(s)) for s in a.get_editor_property('shapes')}
            record['glass']=a.get_editor_property('glass_material').get_path_name()
            record['outline']=a.get_editor_property('outline_material').get_path_name()
        result[a.get_path_name()]=record
    return result
