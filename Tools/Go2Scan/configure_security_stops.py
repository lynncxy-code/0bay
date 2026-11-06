"""CLI editor commandlet: six pauses on the existing office spline, no point edits."""
import json
import shutil
from datetime import datetime
from pathlib import Path
import unreal as u

root=Path('D:/0bay/OfficeShell')
out=root/'Saved/SecurityIntroBuild'
out.mkdir(parents=True,exist_ok=True)
package='/Game/Robots/UnitreeGo2/Maps/L_Go2_TempoOffice'
filename=root/'Content/Robots/UnitreeGo2/Maps/L_Go2_TempoOffice.umap'
backup=out/('L_Go2_TempoOffice-'+datetime.now().strftime('%Y%m%d-%H%M%S')+'.umap')
shutil.copy2(filename,backup)
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert levels.load_level(package)
actors=u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
dogs=[a for a in actors if isinstance(a,u.TempoGo2Course) and a.get_actor_label()=='Go2_Tempo_OfficeRoute']
assert len(dogs)==1
dog=dogs[0]
scanners=[a for a in actors if isinstance(a,u.Go2DeviceScanner)]
assert len(scanners)==1
scanner=scanners[0]
spline=dog.route
def geometry():
    return dict(transform=dog.get_actor_transform().export_text(),closed=spline.is_closed_loop(),
        points=[dict(position=spline.get_location_at_spline_point(i,u.SplineCoordinateSpace.LOCAL).to_tuple(),
                     tangent=spline.get_tangent_at_spline_point(i,u.SplineCoordinateSpace.LOCAL).to_tuple(),
                     type=str(spline.get_spline_point_type(i))) for i in range(spline.get_number_of_spline_points())])
before=geometry()
old=list(dog.legs)
assert old
legs=[]
for i in range(6):
    midpoint=(i+.5)/6
    previous=next((leg for leg in old if midpoint<=leg.end_fraction),old[-1])
    leg=u.TempoGo2Leg()
    leg.end_fraction=(i+1)/6
    leg.speed=previous.speed
    leg.hold_seconds=max(5.0,scanner.scan_duration+.5)
    legs.append(leg)
dog.modify()
dog.set_editor_property('legs',legs)
scanner.modify()
scanner.set_editor_property('security_intro_seconds',20.0)
scanner.set_editor_property('security_scan_delay_seconds',8.0)
scanner.set_editor_property('use_demo_camera',False)
scanner.set_editor_property('auto_scan',False)
assert geometry()==before
assert levels.save_current_level()
assert levels.load_level(package)
actors=u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
dog=next(a for a in actors if isinstance(a,u.TempoGo2Course))
spline=dog.route
assert len(dog.legs)==6 and geometry()==before
report=dict(passed=True,backup=str(backup),route_geometry_unchanged=True,geometry=before,
    stops=[dict(fraction=l.end_fraction,speed=l.speed,hold_seconds=l.hold_seconds,
        location=str(spline.get_location_at_distance_along_spline(l.end_fraction*spline.get_spline_length(),u.SplineCoordinateSpace.WORLD))) for l in dog.legs])
(out/'six-stops.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('SECURITY_SIX_STOPS_SAVED')

