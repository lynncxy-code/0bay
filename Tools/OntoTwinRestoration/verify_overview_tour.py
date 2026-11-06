"""Real-map one-shot tour regression; runtime changes only, never saves assets."""
import json
import time
import traceback
from pathlib import Path
import unreal as u

out = Path('D:/0bay/OfficeShell/Saved/OverviewTourInputFix')
out.mkdir(parents=True, exist_ok=True)
s = dict(start=time.monotonic(), stage=0, due=0, samples=[], handle=None)

def finish(passed, error=None):
    (out / 'runtime.json').write_text(json.dumps(dict(passed=passed, error=error,
        samples=s['samples']), indent=2), encoding='utf-8')
    u.unregister_slate_post_tick_callback(s['handle'])
    u.SystemLibrary.quit_editor()

def tick(dt):
    try:
        now = time.monotonic()
        if now - s['start'] > 200: raise RuntimeError('Tour verification timed out')
        if now < s['due'] or now - s['start'] < 18: return
        world = u.find_object(None, '/Game/OfficeShell/Maps/Office_Demo_ArtMerged.Office_Demo_ArtMerged')
        if not world: return
        if s['stage'] == 0:
            managers = u.GameplayStatics.get_all_actors_of_class(world, u.TwinSceneManager)
            if len(managers) != 1: return
            s['web'] = managers[0].web_interaction_manager
            s['camera'] = next(a for a in u.GameplayStatics.get_all_actors_of_class(world, u.CameraActor)
                if 'OntoTwinViewForBusiness=bv.network-security' in [str(t) for t in a.tags])
            s['authored'] = s['camera'].get_actor_transform().export_text()
            assert s['web'].open_business_view('bv.network-security', '')
            if '-TourInputQuick' in u.SystemLibrary.get_command_line():
                u.GameplayStatics.set_global_time_dilation(world, 10.0)
            s.update(stage=1, due=now+2)
            return
        pawns = u.GameplayStatics.get_all_actors_of_class(world, u.TwinGodViewPawn)
        assert pawns, 'Overview pawn missing'
        pawn = pawns[0]
        assert s['camera'].get_actor_transform().export_text() == s['authored'], 'Authored camera was mutated'
        elapsed = pawn.get_overview_tour_elapsed()
        if s['stage'] == 1:
            assert pawn.is_overview_tour_active(), 'Tour did not start or stopped early at ' + str(elapsed)
            for boundary in [4, 20, 35, 55, 69]:
                if elapsed >= boundary and boundary not in s.get('captured', []):
                    pos = pawn.get_actor_location()
                    s['samples'].append(dict(second=elapsed, position=[pos.x, pos.y, pos.z]))
                    s.setdefault('captured', []).append(boundary)
                    if boundary >= 20:
                        counts = dict(green=0, orange=0, red=0)
                        for actor in u.GameplayStatics.get_all_actors_of_class(world, u.Actor):
                            if not actor.actor_has_tag('MatrixDeviceLink'): continue
                            for comp in actor.get_components_by_class(u.SplineMeshComponent):
                                colour = comp.get_material(0).get_vector_parameter_value('Color')
                                peak = max(colour.r, colour.g, colour.b)
                                rgb = [colour.r/peak, colour.g/peak, colour.b/peak]
                                palettes = dict(green=[.01, 1, .08], orange=[1, .22, .005], red=[1, .005, .005])
                                matches = [name for name, expected in palettes.items()
                                    if max(abs(a-b) for a,b in zip(rgb,expected)) < .001]
                                assert matches, ('Unexpected pulse colour', rgb)
                                counts[matches[0]] += 1
                        assert counts['green'] > 0, counts
                        s['samples'].append(dict(second=elapsed, pulse_colours=counts))
                    u.SystemLibrary.execute_console_command(world,
                        f'HighResShot 1 filename={out.as_posix()}/tour-{boundary}.png')
            if elapsed >= 69: s.update(stage=2, due=now+3)
        elif s['stage'] == 2:
            u.GameplayStatics.set_global_time_dilation(world, 1.0)
            assert not pawn.is_overview_tour_active(), 'Tour must finish once'
            assert abs(elapsed - 70) < .01
            lens = s['camera'].get_component_by_class(u.CameraComponent)
            assert (pawn.get_actor_location()-lens.get_world_location()).length() < .1
            s['manual_start'] = pawn.get_actor_location()
            u.SystemLibrary.execute_console_command(world, 'OntoTwin.Test.CameraKey W down')
            s.update(stage=20, due=now+1)
        elif s['stage'] == 20:
            distance = (pawn.get_actor_location()-s['manual_start']).length()
            assert distance > 10, ('W input did not move completed camera', distance)
            u.SystemLibrary.execute_console_command(world, 'OntoTwin.Test.CameraKey W up')
            s['samples'].append(dict(post_tour_keyboard_moved_cm=distance))
            s.update(stage=21, due=now+2)
        elif s['stage'] == 21:
            s['stopped'] = pawn.get_actor_location()
            s['rotation'] = pawn.get_actor_rotation()
            u.GameplayStatics.get_player_controller(world, 0).set_mouse_location(640, 360)
            u.SystemLibrary.execute_console_command(world, 'OntoTwin.Test.CameraKey RightMouseButton down')
            s.update(stage=22, due=now+.2)
        elif s['stage'] == 22:
            u.SystemLibrary.execute_console_command(world, 'OntoTwin.Test.CameraKey MouseX 20')
            s.update(stage=23, due=now+.3)
        elif s['stage'] == 23:
            u.SystemLibrary.execute_console_command(world, 'OntoTwin.Test.CameraKey RightMouseButton up')
            assert abs(pawn.get_actor_rotation().yaw-s['rotation'].yaw) > .1, 'Mouse look did not respond'
            assert (pawn.get_actor_location()-s['stopped']).length() < .1, 'Drift after key release'
            assert not pawn.is_overview_tour_active(), 'Manual control must not resume tour'
            s['samples'].append(dict(post_tour_mouse_look=True, no_drift_after_release=True))
            assert s['web'].open_business_view('bv.network-security', '')
            s.update(stage=3, due=now+3)
        elif s['stage'] == 3:
            assert pawn.is_overview_tour_active(), 'Explicit re-entry must start a new run'
            assert s['web'].open_instance_detail('17')
            assert not pawn.is_overview_tour_active(), 'Detail entry must cancel immediately'
            s['web'].back()
            s.update(stage=4, due=now+4)
        elif s['stage'] == 4:
            assert not pawn.is_overview_tour_active(), 'Back must not replay'
            assert s['web'].open_business_view('bv.network-security', '')
            s.update(stage=5, due=now+3)
        elif s['stage'] == 5:
            assert pawn.is_overview_tour_active()
            s['web'].close()
            assert not pawn.is_overview_tour_active(), 'Close must cancel immediately'
            finish(True)
    except Exception:
        finish(False, traceback.format_exc())

s['handle'] = u.register_slate_post_tick_callback(tick)
