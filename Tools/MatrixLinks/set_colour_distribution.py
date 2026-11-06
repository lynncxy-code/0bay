"""Initialize communication arc colours from the current device statuses."""
import unreal as u
import json, re, urllib.request, shutil, datetime
from pathlib import Path

root = Path('D:/0bay/OfficeShell')
out = root / 'Saved/MatrixLinks'
lib = u.EditorAssetLibrary
ml = u.MaterialEditingLibrary
actors = u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
links = sorted([a for a in actors if a.actor_has_tag('MatrixDeviceLink')], key=lambda a: a.get_path_name())
assert links, 'Load the office scene before assigning pulse colours.'
count = len(links)
previous = json.loads((out / 'pulse_colour_distribution.json').read_text())
sources = {r['actor']: '/Game/MatrixDeviceLinks/' + re.sub(r'_(Blue|Orange|Red)$', '', r['material'].split('/')[-1].split('.')[0]) for r in previous['records']}
request = urllib.request.Request('http://127.0.0.1:5000/api/v2/state/snapshots',
    headers={'X-OntoTwin-UE-Project-Id': 'ueproj_0bay_office_opensteel_7e0ae9f7'})
with urllib.request.urlopen(request, timeout=15) as response:
    snapshots = json.load(response)
def severity(snapshot):
    status = snapshot.get('interfaces', {}).get('I3D_Overlay', {}).get('resolved_slots', {}).get('status', {})
    label = status.get('display_value', '').strip().lower()
    if any(word in label for word in ['高危', '高风险']) or label in ['critical', 'high risk']:
        return 2
    if '故障' in label or label in ['fault', 'faulty']:
        return 1
    if label:
        return 0
    return {'critical': 2, 'high_risk': 2, 'fault': 1, 'faulty': 1}.get(status.get('level', '').lower(), 0)
states = {item['instanceId']: severity(item) for item in snapshots}
colours = []
for actor in links:
    ids = [str(tag).split('=', 1)[1] for tag in actor.tags if str(tag).startswith(('SourceId=', 'TargetId='))]
    assert len(ids) == 2 and all(i in states for i in ids), ('Missing device status', actor.get_name(), ids)
    colours.append(['Blue', 'Orange', 'Red'][max(states[i] for i in ids)])
blue, orange, red = (colours.count(name) for name in ['Blue', 'Orange', 'Red'])
# Keep legacy material/report identifiers compatible; normal-state colour is green.
palette = {'Blue': (.01, 1.0, .08), 'Orange': (1.0, .22, .005), 'Red': (1.0, .005, .005)}
backup = out / ('PulseColourBackup_' + datetime.datetime.now().strftime('%Y%m%d_%H%M%S'))
backup.mkdir()
shutil.copy2(root / 'Content/MatrixDeviceLinks/L_MatrixDeviceLinks.umap', backup / 'L_MatrixDeviceLinks.umap')
variants = {}
records = []
for actor, colour in zip(links, colours):
    components = actor.get_components_by_class(u.SplineMeshComponent)
    assert components
    for component in components:
        source_path = sources.get(actor.get_path_name())
        if source_path:
            source = lib.load_asset(source_path)
        else:
            source = component.get_material(0)
        assert isinstance(source, u.MaterialInstanceConstant)
        key = (source.get_path_name(), colour)
        if key not in variants:
            destination = '/Game/MatrixDeviceLinks/Colours/' + re.sub(r'_(Blue|Orange|Red)$', '', source.get_name()) + '_' + colour
            material = lib.load_asset(destination) if lib.does_asset_exist(destination) else lib.duplicate_asset(source.get_path_name(), destination)
            old = ml.get_material_instance_vector_parameter_value(source, 'Color')
            brightness = max(old.r, old.g, old.b)
            rgb = palette[colour]
            ml.set_material_instance_vector_parameter_value(material, 'Color', u.LinearColor(*(v * brightness for v in rgb), 1))
            ml.update_material_instance(material)
            assert lib.save_loaded_asset(material)
            variants[key] = material
        component.modify()
        component.set_material(0, variants[key])
    records.append({'actor': actor.get_path_name(), 'colour': colour, 'material': component.get_material(0).get_path_name()})
world = links[0].get_level().get_outer()
assert lib.save_loaded_asset(world, False), 'Failed to save communication sublevel'
for actor, record in zip(links, records):
    for component in actor.get_components_by_class(u.SplineMeshComponent):
        assert component.get_material(0).get_path_name().endswith('_' + record['colour'] + '.' + component.get_material(0).get_name())
result = {'ok': True, 'total': count, 'counts': {'Blue': blue, 'Orange': orange, 'Red': red}, 'saved_world': world.get_path_name(), 'records': records}
(out / 'pulse_colour_distribution.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
print('PULSE_COLOURS_SAVED ' + str(result['counts']))
