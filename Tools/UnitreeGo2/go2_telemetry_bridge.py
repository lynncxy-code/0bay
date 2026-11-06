"""Read-only Go2 DDS -> local Unreal UDP bridge. No publisher or command client is created."""
from __future__ import annotations
import argparse, ipaddress, json, math, os, socket, sys, time, uuid
from pathlib import Path
from xml.sax.saxutils import escape

HERE=Path(__file__).resolve().parent
PROJECT=HERE.parents[1]
ORDER=[f'{leg}_{joint}_joint' for leg in ['FR','FL','RR','RL'] for joint in ['hip','thigh','calf']]

def vector(values, count):
    result=[float(x) for x in values]
    if len(result)!=count or not all(math.isfinite(x) for x in result):raise ValueError('Invalid telemetry vector')
    return result

def low_frame(sample, robot_ip, session, seq, sample_age_ms):
    q=vector([m.q for m in sample.motor_state[:12]],12)
    if any(abs(x)>6.5 for x in q):raise ValueError('Joint angle outside accepted visualization range')
    quat=vector(sample.imu_state.quaternion,4)
    if not 0.25<=sum(x*x for x in quat)<=2.25:raise ValueError('Invalid IMU quaternion')
    return {'schema':'go2.telemetry.v1','source':'unitree_dds','robot_ip':robot_ip,'session':session,'seq':seq,
        'sample_age_ms':sample_age_ms,'robot_tick_ms':int(sample.tick),'robot_serial':[int(x) for x in sample.sn],
        'q':q,'imu_quat_wxyz':quat}

def dds_config(interface_ip, robot_ip, multicast):
    return f'''<CycloneDDS><Domain Id="any"><General>
      <Interfaces><NetworkInterface address="{escape(interface_ip)}"/></Interfaces>
      <AllowMulticast>{'true' if multicast else 'false'}</AllowMulticast>
      </General><Discovery><ParticipantIndex>auto</ParticipantIndex><MaxAutoParticipantIndex>32</MaxAutoParticipantIndex>
      <Peers><Peer Address="{escape(robot_ip)}"/></Peers></Discovery></Domain></CycloneDDS>'''

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--robot-ip',default='192.168.123.18',type=lambda x:str(ipaddress.IPv4Address(x)))
    p.add_argument('--interface-ip',type=lambda x:str(ipaddress.IPv4Address(x)))
    p.add_argument('--port',type=int,default=17702)
    p.add_argument('--hz',type=float,default=60)
    p.add_argument('--duration',type=float,default=0,help='0 means continue until Ctrl+C')
    p.add_argument('--multicast',action='store_true',help='Enable LAN discovery in addition to the selected unicast peer')
    p.add_argument('--synthetic',action='store_true',help='Explicit offline test, never marked as live')
    p.add_argument('--status-file',type=Path,default=PROJECT/'Saved/Go2/bridge_status.json')
    p.add_argument('--record',type=Path,help='Optional local NDJSON recording')
    args=p.parse_args()
    if not 1024<=args.port<=65535 or not 1<=args.hz<=200:p.error('Invalid port or rate')
    if not args.synthetic and not args.interface_ip:p.error('--interface-ip is required for live DDS; choose the robot-facing adapter IP')
    session=str(uuid.uuid4());sender=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
    target=('127.0.0.1',args.port)
    args.status_file.parent.mkdir(parents=True,exist_ok=True)
    record=args.record.open('a',encoding='utf-8') if args.record else None
    start=time.monotonic();last_status=-100.;last_send=-100.;seq=0;low_count=0;sport_count=0;serial=None
    low=None;sport=None;low_at=0.;sport_at=0.;last_tick=None
    state={'status':'initializing','robot_ip':args.robot_ip,'interface_ip':args.interface_ip,'source':'synthetic' if args.synthetic else 'unitree_dds','read_only':True}
    def status(now,force=False):
        nonlocal last_status
        if not force and now-last_status<2:return
        last_status=now
        state.update(frames_forwarded=seq,low_samples=low_count,sport_samples=sport_count,robot_serial=serial,
                     last_joint_sample_age_ms=None if not low_at else (now-low_at)*1000)
        tmp=args.status_file.with_suffix('.tmp');tmp.write_text(json.dumps(state,indent=2),encoding='utf-8');os.replace(tmp,args.status_file)
        print(json.dumps(state),flush=True)
    status(start,True)
    try:
        if args.synthetic:
            data=json.loads((PROJECT/'SourceAssets/UnitreeGo2/Official/motion_reference/stand_poses.json').read_text())
            up=data['poses']['stand_up_joint_pos'];down=data['poses']['stand_down_joint_pos']
        else:
            # Fail early if the requested source IP is not assigned to this computer.
            with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as probe:probe.bind((args.interface_ip,0))
            sys.path.insert(0,str(HERE/'third_party/unitree_sdk2_python-master'))
            from cyclonedds.domain import Domain, DomainParticipant
            from cyclonedds.topic import Topic
            from cyclonedds.sub import DataReader
            from cyclonedds.qos import Qos, Policy
            from unitree_sdk2py.idl.unitree_go.msg.dds_ import LowState_, SportModeState_
            domain=Domain(0,dds_config(args.interface_ip,args.robot_ip,args.multicast))
            participant=DomainParticipant(0)
            qos=Qos(Policy.Reliability.BestEffort,Policy.History.KeepLast(1),Policy.Durability.Volatile)
            low_reader=DataReader(participant,Topic(participant,'rt/lowstate',LowState_),qos=qos)
            sport_reader=DataReader(participant,Topic(participant,'rt/sportmodestate',SportModeState_),qos=qos)
            state['status']='waiting_for_dds'
        while not args.duration or time.monotonic()-start<args.duration:
            now=time.monotonic();packet=None
            if args.synthetic:
                a=(1-math.cos((now-start)*math.pi/3))/2
                packet={'schema':'go2.telemetry.v1','source':'synthetic','robot_ip':args.robot_ip,'session':session,'seq':seq,
                    'sample_age_ms':0,'q':[u+(d-u)*a for u,d in zip(up,down)],'imu_quat_wxyz':[1,0,0,0], 'mode':-1,'gait_type':-1}
                state['status']='synthetic_test_not_live';low_at=now
            else:
                for sample in low_reader.take(32):
                    if not isinstance(sample,LowState_):continue
                    incoming=tuple(int(x) for x in sample.sn)
                    if serial is not None and tuple(serial)!=incoming:raise RuntimeError('Multiple robot serial numbers detected; select a single DDS source')
                    serial=list(incoming)
                    # DDS/bridge repeats must not keep a stopped robot data stream looking fresh.
                    if sample.tick==last_tick:continue
                    low=sample;last_tick=sample.tick;low_at=now;low_count+=1
                for sample in sport_reader.take(16):
                    if isinstance(sample,SportModeState_):sport=sample;sport_at=now;sport_count+=1
                if low is not None and now-low_at<0.2:
                    packet=low_frame(low,args.robot_ip,session,seq,(now-low_at)*1000)
                    if sport is not None and now-sport_at<0.2:
                        packet.update(position_m=vector(sport.position,3),velocity_mps=vector(sport.velocity,3),
                            mode=int(sport.mode),gait_type=int(sport.gait_type),progress=float(sport.progress))
                    state['status']='receiving_live_dds'
                else:state['status']='stale_dds' if low is not None else 'waiting_for_dds'
            if packet is not None and now-last_send>=1/args.hz:
                sender.sendto(json.dumps(packet,allow_nan=False,separators=(',',':')).encode(),target)
                if record:record.write(json.dumps(packet,allow_nan=False)+'\n');record.flush()
                seq+=1;last_send=now
            status(now);time.sleep(0.002)
        state['status']='stopped';status(time.monotonic(),True)
    except KeyboardInterrupt:
        state['status']='stopped';status(time.monotonic(),True)
    except Exception as exc:
        state.update(status='error',error=str(exc));status(time.monotonic(),True);raise
    finally:
        sender.close()
        if record:record.close()

if __name__=='__main__':main()
