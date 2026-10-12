import unreal, json, os

OUT = os.path.join(unreal.Paths.project_saved_dir(), 'VideoTwinMVP')
os.makedirs(OUT, exist_ok=True)
world = unreal.EditorLoadingAndSavingUtils.load_map('/Game/OfficeShell/Maps/Office_Demo')
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
data = []
for a in sub.get_all_level_actors():
    label = a.get_actor_label()
    cams = a.get_components_by_class(unreal.CameraComponent)
    loc, rot = a.get_actor_location(), a.get_actor_rotation()
    item = dict(label=label, name=a.get_name(), path=a.get_path_name(), cls=a.get_class().get_name(), location=[loc.x,loc.y,loc.z], rotation=[rot.pitch,rot.yaw,rot.roll], cameras=[])
    for c in cams:
        p, r = c.get_world_location(), c.get_world_rotation()
        item['cameras'].append(dict(name=c.get_name(),location=[p.x,p.y,p.z],rotation=[r.pitch,r.yaw,r.roll],fov=c.field_of_view,aspect=c.aspect_ratio))
    if cams or '17' in label or '摄像' in label or 'camera' in label.lower():
        unreal.log('VIDEOTWIN_CAMERA ' + json.dumps(item,ensure_ascii=False))
    data.append(item)
with open(os.path.join(OUT,'scene_inventory.json'),'w',encoding='utf-8') as f:
    json.dump(data,f,ensure_ascii=False,indent=2)
unreal.log('VIDEOTWIN_INSPECT_DONE actors=' + str(len(data)))
