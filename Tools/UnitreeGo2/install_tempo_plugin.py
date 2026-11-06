"""Fetch the pinned upstream content plugin and verify each Git blob before installation."""
from pathlib import Path
import json,hashlib,subprocess,concurrent.futures,shutil
P=Path(__file__).resolve().parents[2]
tree=json.loads((P/'SourceAssets/UnitreeGo2/tempo_tree.json').read_text())
dest=P/'Plugins/TempoQuadruped';cache=Path(__file__).parent/'third_party/TempoQuadruped_reference'
def blob_hash(data):return hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
def fetch(entry):
    path=entry['path'];target=dest/path;target.parent.mkdir(parents=True,exist_ok=True)
    for source in [target,cache/path]:
        if source.exists() and blob_hash(source.read_bytes())==entry['sha']:
            if source!=target:shutil.copy2(source,target)
            return path
    partial=target.with_suffix(target.suffix+'.download')
    url=f"https://raw.githubusercontent.com/tempo-sim/TempoQuadruped/{tree['sha']}/{path}"
    subprocess.run(['curl.exe','-L','--fail','--retry','3','--max-time','240','-sS',url,'-o',str(partial)],check=True)
    data=partial.read_bytes();assert len(data)==entry['size'] and blob_hash(data)==entry['sha'],path
    partial.replace(target);return path
entries=[e for e in tree['tree'] if e['type']=='blob']
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    for path in pool.map(fetch,entries):print('VERIFIED',path,flush=True)
project=P/'ShanghaiOfficeShell_OpenSteel.uproject'
backup=P/'Saved/Go2/uproject_before_tempo.json'
if not backup.exists():shutil.copy2(project,backup)
data=json.loads(project.read_text(encoding='utf-8-sig'))
for name in ['TempoQuadruped','Locomotor','ControlRig','FullBodyIK']:
    item=next((p for p in data['Plugins'] if p['Name']==name),None)
    if item:item['Enabled']=True
    else:data['Plugins'].append({'Name':name,'Enabled':True})
project.write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
(P/'SourceAssets/UnitreeGo2/Generated/tempo_plugin_install.json').write_text(json.dumps({'repository':'https://github.com/tempo-sim/TempoQuadruped','commit':tree['sha'],'files_verified':len(entries),'license':'Apache-2.0; preserve upstream model attribution in README','plugin':str(dest)},indent=2))
print('TEMPO_PLUGIN_INSTALLED',flush=True)
