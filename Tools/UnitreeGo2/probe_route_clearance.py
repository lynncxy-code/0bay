import unreal as u,json
from pathlib import Path
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
actors=u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
dogs=[a for a in actors if isinstance(a,u.Go2RouteActor)]
report={'candidates':[]}
for x,y in [(2500,-1400),(2500,-1300),(2800,-1300),(3000,-1400),(2300,-1500),(2100,-1300),(2100,-1450),(3000,-1250),(3000,-1550)]:
    hits=u.SystemLibrary.box_trace_multi(world,u.Vector(x,y,4350),u.Vector(x+200,y,4350),u.Vector(40,25,25),u.Rotator(),u.TraceTypeQuery.TRACE_TYPE_QUERY1,True,dogs,u.DrawDebugTrace.NONE,True)
    report['candidates'].append({'start':[x,y,4320],'hit_count':len(hits) if hits else 0,'hits':[str(h.to_tuple()) for h in hits] if hits else []})
(Path(u.Paths.project_saved_dir())/'Go2/route_clearance.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
