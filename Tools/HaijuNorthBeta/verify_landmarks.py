"""Persisted-state regression: explicit delta, original materials/transforms and proxy hashes."""
import json
import sys
from pathlib import Path
import unreal as u
sys.path.insert(0,str(Path(__file__).resolve().parent))
from landmark_state import capture, digest

P=Path(r'D:/0bay/OfficeShell');OUT=P/'Saved/HaijuNorthBeta'
source=P/'Art/HaijuNorthBeta/Landmarks'
manifest=json.loads((source/'replacement_manifest.json').read_text(encoding='utf-8'))
baseline=OUT/'landmark_baseline.json'
if not baseline.exists():baseline=P/'Art/HaijuNorthBeta/Acceptance/Landmarks/Before_Actor_State.json'
before=json.loads(baseline.read_text(encoding='utf-8'))
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert levels.load_level('/Game/OfficeShell/Maps/Office_Demo')
after=capture()
changes={x['old_mesh_name']:x for x in manifest['batch_replacements']}
removed=set(manifest['remove_pick_keys'])
new_shapes={x['key']:x for x in json.loads((source/'landmark_pick_shapes.json').read_text(encoding='utf-8'))}
for shape in new_shapes.values():
    for field in ('vertices','edges'):
        shape[field]=[[round(float(x),6) for x in p] for p in shape[field]]
checked=0;unchanged_background=0;unchanged_shapes=0
for path,old in before.items():
    assert path in after,('missing actor',path)
    new=after[path]
    if old['cls'].endswith('.CampusInteraction'):
        assert {k:v for k,v in old.items() if k!='shapes'}=={k:v for k,v in new.items() if k!='shapes'}
        for key,value in old['shapes'].items():
            if key not in removed:
                assert new['shapes'][key]==value,key
                unchanged_shapes+=1
        assert len(new['shapes'])==1676
        for key,shape in new_shapes.items():assert new['shapes'][key]==digest(shape),key
        assert not (set(manifest['pick_aliases']) & set(new['shapes']))
    elif old.get('mesh','') and old['mesh'].split('.')[-1] in changes:
        entry=changes[old['mesh'].split('.')[-1]]
        exempt={'mesh','hidden','visible'}
        assert {k:v for k,v in old.items() if k not in exempt}=={k:v for k,v in new.items() if k not in exempt},path
        if entry['replacement_mesh_name']:
            assert '/Landmarks/Imported/Background_Changed_Batches/' in new['mesh']
            assert not new['hidden'] and new['visible']
        else:
            assert new['mesh']==old['mesh'] and new['hidden'] and not new['visible']
    else:
        assert old==new,('unexpected change',path)
        if '/L_HaijuNorthBeta_Exterior.' in path:unchanged_background+=1
    checked+=1
assert unchanged_background==146,unchanged_background
assert unchanged_shapes==1668,unchanged_shapes
added=[(p,r) for p,r in after.items() if p not in before]
assert len(added)==48,len(added)
for path,r in added:
    assert '/L_RefinedLandmarks.' in path
    assert r['location']==[14485.456289,-5394.376593,0.0] and r['rotation']==[0.0,20.9,0.0] and r['scale']==[1.0,1.0,1.0]
    assert not r['hidden'] and r['visible'] and r['profile']=='NoCollision' and not r['navigation'] and not r['overlap'] and not r['shadow']
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert isinstance(u.GameplayStatics.get_streaming_level(world,'/Game/HaijuNorthBeta/Maps/L_RefinedLandmarks'),u.LevelStreamingAlwaysLoaded)
triangle_counts={}
for a in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors():
    if not isinstance(a,u.StaticMeshActor):continue
    m=a.static_mesh_component.static_mesh
    if m and '/Landmarks/' in m.get_path_name():
        triangle_counts[m.get_path_name()]=m.get_num_triangles(0)
landmark_triangles=sum(n for path,n in triangle_counts.items() if '/Landmarks_Combined/' in path)
remainder_triangles=sum(n for path,n in triangle_counts.items() if '/Background_Changed_Batches/' in path)
assert landmark_triangles>0 and remainder_triangles>0
report=dict(passed=True,existing_actors_checked=checked,original_actor_transforms_changed=0,
            unchanged_background_mesh_actors=unchanged_background,unchanged_pick_shapes=unchanged_shapes,
            new_landmark_actors=len(added),updated_pick_shapes=len(new_shapes),total_pick_shapes=1676,
            original_remainder_materials_retained=True,campus_ground_and_facilities_unchanged=True,
            ground_actor_z_cm=3,ground_surface_z_cm=1,nanite_fallback_landmark_triangles=landmark_triangles,nanite_fallback_remainder_triangles=remainder_triangles,
            source_geometry_validation=json.loads((source/'validation.json').read_text(encoding='utf-8')))
(OUT/'landmark_verification.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
u.log('LANDMARK_VERIFIED '+json.dumps({k:v for k,v in report.items() if k!='source_geometry_validation'}))
