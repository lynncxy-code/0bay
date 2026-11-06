import unreal as u,json,time,traceback
from pathlib import Path
out=Path('D:/0bay/OfficeShell/Saved/CloudWorkspace')
groups=json.loads((out/'groups.json').read_text(encoding='utf-8'))
expected_proxies=len(groups)
expected_sources=sum(len(g['actors']) for g in groups)
cloud_test=dict(stage=0,start=time.monotonic(),due=0,report={},handle=None)
def cloud_finish(error=None):
    cloud_test['report'].update(ok=error is None,error=error)
    (out/'runtime-verification.json').write_text(json.dumps(cloud_test['report'],ensure_ascii=False,indent=2),encoding='utf8')
    u.unregister_slate_post_tick_callback(cloud_test['handle'])
    u.EditorLevelLibrary.editor_end_play()
def cloud_tick(dt):
    try:
        now=time.monotonic()
        if now-cloud_test['start']>180:raise RuntimeError('Runtime validation timed out')
        world=u.EditorLevelLibrary.get_game_world()
        if not world or now<cloud_test['due']:return
        tagged=u.GameplayStatics.get_all_actors_with_tag(world,'OntoTwinSecurityOnly')
        proxies=[a for a in tagged if isinstance(a,u.TwinInstance)]
        sources=[a for a in tagged if not isinstance(a,u.TwinInstance)]
        def record():
            return dict(sources=len(sources),proxies=len(proxies),hidden=sum(a.get_editor_property('hidden') for a in tagged),
                names=[a.get_editor_property('twin_display_name') for a in proxies],
                panels=[dict(name=a.get_editor_property('twin_display_name'),visible=c.is_visible(),hidden=c.get_editor_property('hidden_in_game')) for a in proxies for c in a.get_components_by_class(u.WidgetComponent)],
                beams=[dict(actor=a.get_name(),active=c.is_active(),particles=c.get_num_active_particles(),visible=c.is_visible(),hidden=c.get_editor_property('hidden_in_game')) for a in sources for c in a.get_components_by_class(u.ParticleSystemComponent)],
                placeholder_meshes=sum(c.static_mesh is not None for a in proxies for c in a.get_components_by_class(u.StaticMeshComponent)))
        stage=cloud_test['stage']
        if stage==0:
            if len(proxies)!=expected_proxies or now-cloud_test['start']<12:return
            assert len(sources)==expected_sources
            cloud_test['report']['startup']=record();assert all(a.get_editor_property('hidden') for a in tagged)
            manager=u.GameplayStatics.get_all_actors_of_class(world,u.TwinSceneManager)[0]
            cloud_test['web']=manager.get_editor_property('web_interaction_manager')
            assert cloud_test['web'].open_business_view('bv.network-security','')
            cloud_test.update(stage=1,due=now+35)
        elif stage==1:
            cloud_test['report']['security']=record()
            assert len(proxies)==expected_proxies and all(not a.get_editor_property('hidden') for a in tagged)
            assert cloud_test['report']['security']['placeholder_meshes']==0
            assert len(cloud_test['report']['security']['panels'])==expected_proxies
            assert all(p['visible'] and not p['hidden'] for p in cloud_test['report']['security']['panels'])
            assert len(cloud_test['report']['security']['beams'])==4
            assert all(b['active'] and b['particles']>0 and b['visible'] and not b['hidden'] for b in cloud_test['report']['security']['beams'])
            cloud_test['web'].back();cloud_test.update(stage=2,due=now+4)
        elif stage==2:
            cloud_test['report']['after_back']=record();assert all(a.get_editor_property('hidden') for a in tagged)
            assert cloud_test['web'].open_business_view('bv.custom.mu0xsto9','')
            cloud_test.update(stage=3,due=now+4)
        else:
            cloud_test['report']['other_business']=record();assert all(a.get_editor_property('hidden') for a in tagged)
            cloud_finish()
    except Exception:cloud_finish(traceback.format_exc())
cloud_test['handle']=u.register_slate_post_tick_callback(cloud_tick)
u.get_editor_subsystem(u.LevelEditorSubsystem).editor_play_simulate()
