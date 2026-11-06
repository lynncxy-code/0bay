"""Add ten distinct cloud-to-cloud links, five originating at the AI assistant."""
import unreal as u,json,math,datetime,shutil,collections
from pathlib import Path
root=Path('D:/0bay/OfficeShell');out=root/'Saved/MatrixLinks'
actors=u.get_editor_subsystem(u.EditorActorSubsystem);levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor()
data=json.loads((out/'communication_demo.json').read_text(encoding='utf-8'));rows=data['connections'];before=len(rows)
assert before==158, 'This one-time addition expects 158 existing connections.'
groups=json.loads((root/'Saved/CloudWorkspace/groups.json').read_text(encoding='utf-8'))
aa=actors.get_all_level_actors();by={a.get_path_name():a for a in aa}
devices={g['instance_id']:{'position':by[g['root']].get_actor_location(),'record':dict(id=g['instance_id'],name=g['display_name'],type='cloud.workspace',actors=[a['path'] for a in g['actors']])} for g in groups}
ai='cloud.workspace.ai-assistant'
def key(a,b):return tuple(sorted((a,b)))
def distance(a,b):return (devices[a]['position']-devices[b]['position']).length()
used={key(r['source']['id'],r['target']['id']) for r in rows}
targets=sorted((i for i in devices if i!=ai and key(ai,i) not in used),key=lambda i:(distance(ai,i),i))[:5]
assert len(targets)==5
pairs=[(ai,i) for i in targets];used.update(key(a,b) for a,b in pairs)
covered={ai,*targets}
for iid in sorted(set(devices)-covered):
    candidates=[j for j in covered if j!=ai and key(iid,j) not in used]
    other=min(candidates,key=lambda j:(distance(iid,j),j))
    pairs.append((other,iid));used.add(key(other,iid));covered.add(iid)
candidates=sorted((distance(a,b),a,b) for a in devices for b in devices if a<b and ai not in (a,b) and key(a,b) not in used)
for _,a,b in candidates:
    if len(pairs)==10:break
    pairs.append((a,b));used.add(key(a,b))
assert len(pairs)==10 and sum(a==ai for a,b in pairs)==5
backup=out/('Cloud10Backup_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'));backup.mkdir()
for file in [root/'Content/MatrixDeviceLinks/L_MatrixDeviceLinks.umap',out/'communication_demo.json',out/'pulse_colour_distribution.json']:
    shutil.copy2(file,backup/file.name)
templates={}
for a in aa:
    if 'STYLE_' in a.get_actor_label() and a.get_component_by_class(u.NiagaraComponent):
        style=int(a.get_actor_label().split('_STYLE_')[1].split('_')[0])
        templates.setdefault(style,a)
assert all(i in templates for i in [2,13,14])
assert levels.set_current_level_by_name('L_MatrixDeviceLinks')
base=by[rows[0]['strips'][0]].get_component_by_class(u.SplineMeshComponent)
world=by[rows[0]['strips'][0]].get_level().get_outer()
report=[]
for index,(source,target) in enumerate(pairs):
    style=[2,13,14][index%3];number=before+index+1
    start=devices[source]['position'];end=devices[target]['position'];d=end-start
    height=min(50.,max(15.,d.length()*.025))
    a=actors.spawn_actor_from_class(u.SplineMeshActor,start)
    c=a.get_component_by_class(u.SplineMeshComponent)
    c.set_static_mesh(base.get_editor_property('static_mesh'));c.set_material(0,base.get_material(0))
    c.set_collision_enabled(u.CollisionEnabled.NO_COLLISION);c.set_editor_property('cast_shadow',False)
    c.set_forward_axis(u.SplineMeshAxis.X,False);c.set_spline_up_dir(u.Vector(0,0,1),False)
    c.set_start_scale(base.get_start_scale(),False);c.set_end_scale(base.get_end_scale(),False)
    c.set_start_and_end(u.Vector(0,0,0),d+u.Vector(0,0,4*height),d,d-u.Vector(0,0,4*height),True)
    tags=['CommunicationDemo','SecurityCommunicationVFX','SourceId='+source,'TargetId='+target,'CloudWorkspaceLink','EffectStyle=%02d'%style]
    a.tags=tags+['MatrixDeviceLink'];a.set_actor_hidden_in_game(True)
    a.set_actor_label('COMM_%03d_CLOUD_STYLE_%02d'%(number,style));a.set_folder_path('VFX/MatrixDeviceLinks/Communication')
    effects=[]
    for j,t in enumerate([.25,.55,.8]):
        position=start+d*t+u.Vector(0,0,4*height*t*(1-t))
        tangent=d+u.Vector(0,0,4*height*(1-2*t))
        effect=actors.duplicate_actor(templates[style],world,position-templates[style].get_actor_location())
        assert effect
        effect.set_actor_rotation(u.MathLibrary.make_rot_from_z(-tangent),True)
        effect.set_actor_label('COMM_%03d_CLOUD_STYLE_%02d_%d'%(number,style,j))
        effect.tags=tags
        effect.set_actor_hidden_in_game(True)
        effect.set_folder_path('VFX/MatrixDeviceLinks/CloudEffects')
        nc=effect.get_component_by_class(u.NiagaraComponent)
        nc.set_auto_activate(False);nc.deactivate()
        assert nc.get_asset().get_name()=='NS_TinyParticles_%02d'%style
        effects.append(effect.get_path_name())
    data['native_alert_emitters'].extend(effects)
    rows.append(dict(line=number,source=devices[source]['record'],target=devices[target]['record'],start=start.to_tuple(),end=end.to_tuple(),pulse_style=rows[0]['pulse_style'],width_cm=rows[0]['width_cm'],strips=[a.get_path_name()],arc_height_cm=height,midpoint_z=(start.z+end.z)/2+height,alert_style='%02d'%style,native_emitters=effects))
    report.append(dict(line=number,source=source,target=target,style='%02d'%style))
assert len({key(r['source']['id'],r['target']['id']) for r in rows})==len(rows)
data['independent_lines']=len(rows)
assert u.EditorAssetLibrary.save_loaded_asset(world,False)
(out/'communication_demo.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
exec(compile((root/'Tools/MatrixLinks/set_colour_distribution.py').read_text(encoding='utf-8'),'set_colour_distribution.py','exec'),{})
result=dict(ok=True,before=before,total=len(rows),added=10,ai_outgoing=5,styles=dict(collections.Counter(r['style'] for r in report)),connections=report)
(out/'cloud10_verification.json').write_text(json.dumps(result,indent=2))
print('CLOUD10_SAVED',result)
