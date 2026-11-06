import unreal as u
import time
import json
from pathlib import Path
s=dict(start=time.monotonic(),stage=0,due=0)
out=Path('D:/0bay/OfficeShell/Saved/SecuritySky')
def tick(dt):
    try:
        w=u.find_object(None,'/Game/OfficeShell/Maps/Office_Demo_ArtMerged.Office_Demo_ArtMerged')
        if not w:return
        now=u.GameplayStatics.get_time_seconds(w)
        if time.monotonic()-s['start']>150:raise RuntimeError('Preview timeout')
        if now<15 or now<s['due']:return
        if s['stage']==0:
            manager=u.GameplayStatics.get_all_actors_of_class(w,u.TwinSceneManager)[0]
            assert manager.web_interaction_manager.open_business_view('bv.network-security','')
            s.update(stage=1,due=now+3)
        elif s['stage']==1:
            scanner=u.GameplayStatics.get_all_actors_of_class(w,u.Go2DeviceScanner)[0]
            if scanner.security_intro_active:return
            s.update(stage=2,due=now+4)
        elif s['stage']==2:
            u.SystemLibrary.execute_console_command(w,'HighResShot 1 filename=D:/0bay/OfficeShell/Saved/SecuritySky/blue-black.png')
            s.update(stage=3,due=now+2)
        else:
            (out/'tone-verification.json').write_text(json.dumps(dict(passed=True)))
            u.unregister_slate_post_tick_callback(s['handle'])
            u.SystemLibrary.quit_editor()
    except Exception as e:
        (out/'tone-verification.json').write_text(json.dumps(dict(passed=False,error=str(e))))
        u.unregister_slate_post_tick_callback(s['handle'])
        u.SystemLibrary.quit_editor()
s['handle']=u.register_slate_post_tick_callback(tick)
