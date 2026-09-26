"""Formal generated-media CPU/CUDA benchmark; no private camera media.

One second per raster/cadence/mode/backend; use --seconds to extend. No test
tolerances are altered. Every timed job must pass the production validator.
Reports app and descendant private memory/thread counts separately from timings.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import json
import os
from pathlib import Path
import subprocess
import time
from integration import run
from sustained import Counters


class Process(C.Structure):
    _fields_=[('size',W.DWORD),('usage',W.DWORD),('pid',W.DWORD),('heap',C.c_size_t),('module',W.DWORD),
              ('threads',W.DWORD),('parent',W.DWORD),('priority',W.LONG),('flags',W.DWORD),('exe',W.WCHAR*260)]


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--cli',type=Path,required=True)
    ap.add_argument('--ffmpeg',type=Path,required=True)
    ap.add_argument('--work',type=Path,required=True)
    ap.add_argument('--seconds',type=float,default=1)
    ap.add_argument('--backends',nargs='+',default=['cpu','cuda'])
    args=ap.parse_args()
    work=args.work.resolve();work.mkdir(parents=True,exist_ok=True)
    cli=args.cli.resolve();ff=args.ffmpeg.resolve()
    env={k.upper():v for k,v in os.environ.items()};env['LOGFORGE_DATA_DIR']=str(work/'appdata')
    run([cli,'--approve-ffmpeg','--ffmpeg',ff],env=env)
    kernel=C.WinDLL('kernel32',use_last_error=True)
    kernel.CreateToolhelp32Snapshot.argtypes=[W.DWORD,W.DWORD];kernel.CreateToolhelp32Snapshot.restype=W.HANDLE
    for name in ('Process32FirstW','Process32NextW'):
        getattr(kernel,name).argtypes=[W.HANDLE,C.POINTER(Process)]
    kernel.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD];kernel.OpenProcess.restype=W.HANDLE
    kernel.CloseHandle.argtypes=[W.HANDLE]
    memory=C.WinDLL('psapi').GetProcessMemoryInfo
    memory.argtypes=[W.HANDLE,C.POINTER(Counters),W.DWORD]
    def observe(pid):
        snapshot=kernel.CreateToolhelp32Snapshot(2,0);entries=[];entry=Process();entry.size=C.sizeof(entry)
        try:
            more=kernel.Process32FirstW(snapshot,C.byref(entry))
            while more:
                entries.append((entry.pid,entry.parent,entry.threads));more=kernel.Process32NextW(snapshot,C.byref(entry))
        finally:kernel.CloseHandle(snapshot)
        own={pid}
        for _ in range(4):own|={p for p,parent,_ in entries if parent in own}
        app=total=threads=0
        for p,_,n in entries:
            if p not in own:continue
            threads+=n;handle=kernel.OpenProcess(0x410,False,p)
            if not handle:continue
            try:
                c=Counters();c.cb=C.sizeof(c)
                if memory(handle,C.byref(c),c.cb):
                    total+=c.private
                    if p==pid:app=c.private
            finally:kernel.CloseHandle(handle)
        return app,total,threads
    results=[]
    for width,height,rate in [(1920,1080,24),(3840,2160,24),(3840,2160,30),(3840,2160,60),(3840,2160,120)]:
        frames=max(3,round(rate*args.seconds));source=work/f'{width}x{height}-{rate}-source.mov'
        run([ff,'-v','error','-y','-f','lavfi','-i',f'nullsrc=size={width}x{height}:rate={rate},format=yuv422p10le,geq=lum=64+876*X/W:cb=512:cr=512,setparams=color_primaries=bt2020:color_trc=arib-std-b67:colorspace=bt2020nc:range=limited',
            '-frames:v',str(frames),'-c:v','prores_ks','-profile:v','3','-threads:v','6','-color_primaries','bt2020','-color_trc','arib-std-b67','-colorspace','bt2020nc',source])
        for backend in args.backends:
            for creative in (False,True):
                name=f'{width}x{height}-{rate}-{backend}-'+('creative' if creative else 'standard')
                output=work/(name+'.mov');output.unlink(missing_ok=True)
                command=[str(cli),'--convert',str(source),str(output),'--ffmpeg',str(ff),'--input-chroma-location','left','--backend',backend]
                if creative:command+=['--tone']
                peaks=[0,0,0];start=time.perf_counter()
                with (work/(name+'.txt')).open('wb') as log:
                    process=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT)
                    while process.poll() is None:
                        peaks=[max(a,b) for a,b in zip(peaks,observe(process.pid))]
                        if time.perf_counter()-start>600:process.terminate();raise RuntimeError('Benchmark timeout')
                        time.sleep(.1)
                    if process.returncode:raise RuntimeError((work/(name+'.txt')).read_text(encoding='utf-8'))
                wall=time.perf_counter()-start
                report=json.loads(max((work/'appdata/logs').glob('*.validation.json'),key=lambda p:p.stat().st_mtime_ns).read_text(encoding='utf-8'))
                assert report['validation']['passed']
                result={'case':name,'width':width,'height':height,'capture_fps':rate,'frames':frames,'creative':creative,'backend':backend,
                        'wall_seconds_including_tool_verification':wall,'fps_including_verification':frames/wall,
                        'peak_app_private_bytes':peaks[0],'peak_tree_private_bytes':peaks[1],'peak_tree_threads':peaks[2],
                        'timing':report['validation']['timing'],'pixel_sanity':report['validation']['signal']['pixel_sanity']}
                results.append(result);(work/'benchmark.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
                print(name,round(wall,3),'seconds; app/tree memory',peaks[:2],flush=True)
                output.unlink()
        source.unlink()


if __name__=='__main__':main()
