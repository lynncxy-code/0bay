"""List the actors of Office_Demo whose bounds overlap a world box (default: camera 17's receiver box), with their
mesh names and bounds. Used to find furniture to keep out of the video (2026-10-16: the planter cabinets).

Run headless:
  UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script=<this file> -unattended -nosplash
Writes Saved/VideoTwin/actors_in_box.tsv and prints a short summary prefixed VTLIST.
"""
import unreal as u
from pathlib import Path

MAP = '/Game/OfficeShell/Maps/Office_Demo'
BOX_MIN = u.Vector(-400, -3400, 4300)
BOX_MAX = u.Vector(2900, -1000, 4800)

u.EditorLoadingAndSavingUtils.load_map(MAP)
actors = u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
rows = []
for a in actors:
    origin, ext = a.get_actor_bounds(False)
    lo = origin - ext
    hi = origin + ext
    if hi.x < BOX_MIN.x or lo.x > BOX_MAX.x or hi.y < BOX_MIN.y or lo.y > BOX_MAX.y or hi.z < BOX_MIN.z or lo.z > BOX_MAX.z:
        continue
    if ext.x > 3000 or ext.y > 3000:
        continue   # floors, ceilings, the building shell
    meshes = []
    for c in a.get_components_by_class(u.StaticMeshComponent):
        m = c.get_editor_property('static_mesh')
        if m:
            meshes.append(m.get_name())
    rows.append((a.get_actor_label(), a.get_class().get_name(), ','.join(sorted(set(meshes)))[:160],
                 lo.x, lo.y, lo.z, hi.x, hi.y, hi.z))
out = Path(u.Paths.project_saved_dir()) / 'VideoTwin' / 'actors_in_box.tsv'
out.parent.mkdir(parents=True, exist_ok=True)
with open(out, 'w', encoding='utf-8') as f:
    f.write('label\tclass\tmeshes\tminx\tminy\tminz\tmaxx\tmaxy\tmaxz\n')
    for r in rows:
        f.write('%s\t%s\t%s\t%.0f\t%.0f\t%.0f\t%.0f\t%.0f\t%.0f\n' % r)
u.log('VTLIST %d actors of %d written to %s' % (len(rows), len(actors), out))
