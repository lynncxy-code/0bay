"""Smooth communication packets and connect the registered cloud workspace instances."""
import unreal as u
import json, math, shutil, datetime
from pathlib import Path
root=Path('D:/0bay/OfficeShell')
out=root/'Saved/MatrixLinks'
lib=u.EditorAssetLibrary
ml=u.MaterialEditingLibrary
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor()
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
all_actors=actors.get_all_level_actors()
by_path={a.get_path_name():a for a in all_actors}
data=json.loads((out/'communication_demo.json').read_text(encoding='utf-8'))
rows=data['connections']
groups=json.loads((root/'Saved/CloudWorkspace/groups.json').read_text(encoding='utf-8'))
members=json.loads((out/'security_88_members.json').read_text(encoding='utf-8'))
backup=out/('SmoothCloudBackup_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'))
backup.mkdir()
for file in [root/'Content/MatrixDeviceLinks/L_MatrixDeviceLinks.umap',root/'Content/MatrixDeviceLinks/Preview/M_CommunicationPulse_Bright.uasset',out/'communication_demo.json',out/'security_88_members.json']:
    shutil.copy2(file,backup/file.name)
mat=lib.load_asset('/Game/MatrixDeviceLinks/Preview/M_CommunicationPulse_Bright')
custom=ml.get_material_property_input_node(mat,u.MaterialProperty.MP_OPACITY)
assert isinstance(custom,u.MaterialExpressionCustom)
custom.set_editor_property('code',"""// Two long packets moving at 0.15 line lengths per second.
float x = UV.x;
float q = frac(x * 2.0 - Clock * 0.30 + Phase);
float packet = smoothstep(0.025, 0.13, q) * (1.0 - smoothstep(0.83, 0.94, q));
float edge = 1.0 - smoothstep(0.27, 0.41, abs(UV.y - 0.5));
float endpoints = smoothstep(0.0, 0.025, x) * smoothstep(0.0, 0.025, 1.0 - x);
return packet * edge * endpoints;
""")
emission=ml.get_material_property_input_node(mat,u.MaterialProperty.MP_EMISSIVE_COLOR)
if not isinstance(emission,u.MaterialExpressionMultiply):
    gain=ml.create_material_expression(mat,u.MaterialExpressionScalarParameter,-200,550)
    gain.set_editor_property('parameter_name','PulseBrightness')
    gain.set_editor_property('default_value',.60)
    multiply=ml.create_material_expression(mat,u.MaterialExpressionMultiply,0,350)
    assert ml.connect_material_expressions(emission,'',multiply,'A')
    assert ml.connect_material_expressions(gain,'',multiply,'B')
    assert ml.connect_material_property(multiply,'',u.MaterialProperty.MP_EMISSIVE_COLOR)
mat.set_editor_property('translucency_pass',u.MaterialTranslucencyPass.MTP_AFTER_MOTION_BLUR)
ml.recompile_material(mat)
assert lib.save_loaded_asset(mat)
servers={}
for row in rows:
    for key,pos in [('source','start'),('target','end')]:
        if row[key]['type']=='security.device.servercabinet':
            servers[row[key]['id']]={'record':row[key],'position':row[pos]}
assert servers
assert levels.set_current_level_by_name('L_MatrixDeviceLinks')
template=by_path[rows[0]['strips'][0]].get_component_by_class(u.SplineMeshComponent)
mesh=template.get_editor_property('static_mesh')
added=[]
for group in groups:
    iid=group['instance_id']
    members[iid]={'name':group['display_name'],'type':'cloud.workspace'}
    if any(iid in [r['source']['id'],r['target']['id']] for r in rows):continue
    target_actor=by_path[group['root']]
    end=target_actor.get_actor_location()
    server=min(servers.values(),key=lambda s:math.dist(s['position'],end.to_tuple()))
    start=u.Vector(*server['position'])
    delta=end-start
    # An elevated cloud endpoint is the upper limit of its connecting arc.
    height=min(70.,abs(delta.z)/4.)
    actor=actors.spawn_actor_from_class(u.SplineMeshActor,start)
    c=actor.get_component_by_class(u.SplineMeshComponent)
    c.set_static_mesh(mesh)
    c.set_material(0,template.get_material(0))
    c.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
    c.set_editor_property('cast_shadow',False)
    c.set_forward_axis(u.SplineMeshAxis.X,False)
    c.set_spline_up_dir(u.Vector(0,0,1),False)
    c.set_start_scale(template.get_start_scale(),False)
    c.set_end_scale(template.get_end_scale(),False)
    c.set_start_and_end(u.Vector(0,0,0),delta+u.Vector(0,0,4*height),delta,delta-u.Vector(0,0,4*height),True)
    number=len(rows)+1
    actor.set_actor_label('COMM_%03d_Cloud_%s'%(number,iid.split('.')[-1]))
    actor.set_folder_path('VFX/MatrixDeviceLinks/Communication')
    actor.tags=['MatrixDeviceLink','CommunicationDemo','SecurityCommunicationVFX','SourceId='+server['record']['id'],'TargetId='+iid]
    actor.set_actor_hidden_in_game(True)
    target={'id':iid,'name':group['display_name'],'type':'cloud.workspace','actors':[a['path'] for a in group['actors']]}
    rows.append(dict(line=number,source=server['record'],target=target,start=start.to_tuple(),end=end.to_tuple(),pulse_style=rows[0]['pulse_style'],width_cm=rows[0]['width_cm'],strips=[actor.get_path_name()],arc_height_cm=height,midpoint_z=(start.z+end.z)/2+height,alert_style=None))
    added.append(iid)
data.update(independent_lines=len(rows),unique_devices=len({r[k]['id'] for r in rows for k in ['source','target']}),eligible_object_types=sorted({r[k]['type'] for r in rows for k in ['source','target']}))
level=by_path[data['connections'][0]['strips'][0]].get_level().get_outer()
assert lib.save_loaded_asset(level,False)
(out/'communication_demo.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
(out/'security_88_members.json').write_text(json.dumps(members,ensure_ascii=False,indent=2),encoding='utf-8')
exec(compile((root/'Tools/MatrixLinks/set_colour_distribution.py').read_text(encoding='utf-8'),'set_colour_distribution.py','exec'),{})
report={'ok':True,'packet_groups':2,'travel_cycles_per_second':.15,'brightness_gain':.60,'cloud_type':'cloud.workspace','cloud_instances_connected':len(groups),'added':added,'lines':len(rows)}
(out/'smooth_cloud_verification.json').write_text(json.dumps(report,indent=2))
print('SMOOTH_CLOUD_SAVED',report)
