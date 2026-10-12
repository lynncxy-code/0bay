"""Encode supplied still plus a visible moving test marker into a local mock video."""
from pathlib import Path
import sys, subprocess, shutil
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Saved/VideoTwinMVP/pydeps'))
import imageio_ffmpeg
reference=ROOT/'Art/VideoTwinMVP/Camera17_Reference.jpg'
reference.parent.mkdir(parents=True,exist_ok=True)
if not reference.exists():
    shutil.copy2(r'C:/Users/ADMIN/AppData/Local/Temp/codex-clipboard-3c07ee8f-f517-4f55-808d-ad461d0759e0.jpg',reference)
movie=ROOT/'Content/Movies/Camera17_Mock.mp4'
movie.parent.mkdir(parents=True,exist_ok=True)
# The moving marker explicitly signals synthetic motion; the office itself is not fabricated.
filt="scale=1920:1080,drawbox=x=20:y=1020:w=540:h=44:color=black@0.8:t=fill,drawtext=fontfile='C\\:/Windows/Fonts/consola.ttf':text='MOCK CAM17  %{pts\\:hms}':x=32:y=1030:fontsize=24:fontcolor=white,drawbox=x=1850:y=30:w=40:h=40:color=cyan:t=fill:enable='lt(mod(t,2),1)'"
subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(),'-hide_banner','-loglevel','warning','-y','-loop','1','-framerate','15','-i',str(reference),'-vf',filt,'-t','12','-c:v','libx264','-preset','fast','-crf','20','-pix_fmt','yuv420p','-movflags','+faststart','-an',str(movie)],check=True)
print(movie, movie.stat().st_size)
