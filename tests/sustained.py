"""Generated 4K120 conversion, 240 frames, private-byte observation and temporal samples."""
import argparse
import array
import ctypes
from ctypes import wintypes
import json
import math
import os
from pathlib import Path
import subprocess
import time
from integration import run


class Counters(ctypes.Structure):
    _fields_ = [('cb', wintypes.DWORD), ('faults', wintypes.DWORD)] + [
        (name, ctypes.c_size_t) for name in ('peak_working', 'working', 'peak_paged', 'paged',
                                          'peak_nonpaged', 'nonpaged', 'pagefile', 'peak_pagefile', 'private')]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cli', required=True, type=Path)
    ap.add_argument('--ffmpeg', required=True, type=Path)
    ap.add_argument('--work', required=True, type=Path)
    args = ap.parse_args()
    work = args.work.resolve(); work.mkdir(parents=True, exist_ok=True)
    cli, ff = args.cli.resolve(), args.ffmpeg.resolve()
    env = {k.upper(): v for k,v in os.environ.items()}; env['LOGFORGE_DATA_DIR'] = str(work/'appdata')
    run([cli,'--approve-ffmpeg','--ffmpeg',ff],env=env)
    source, output = work/'4k120-HLG.mov', work/'4k120-AppleLog.mov'
    if output.exists(): output.unlink()
    run([ff,'-v','error','-y','-f','lavfi','-i',
         'nullsrc=size=3840x2160:rate=120,format=yuv422p10le,geq=lum=64+876*X/W:cb=512:cr=512,setparams=color_primaries=bt2020:color_trc=arib-std-b67:colorspace=bt2020nc:range=limited',
         '-frames:v','240','-c:v','prores_ks','-profile:v','3','-threads:v','4',
         '-color_primaries','bt2020','-color_trc','arib-std-b67','-colorspace','bt2020nc',source])
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes=[wintypes.DWORD,wintypes.BOOL,wintypes.DWORD];kernel.OpenProcess.restype=wintypes.HANDLE
    kernel.CloseHandle.argtypes=[wintypes.HANDLE]
    getmem = ctypes.WinDLL('psapi').GetProcessMemoryInfo
    getmem.argtypes=[wintypes.HANDLE,ctypes.POINTER(Counters),wintypes.DWORD]
    memory=[];start=time.monotonic()
    with (work/'conversion.log').open('wb') as log:
        process=subprocess.Popen([str(cli),'--convert',str(source),str(output),'--ffmpeg',str(ff),
                                  '--input-chroma-location','left'],env=env,stdout=log,stderr=subprocess.STDOUT)
        handle=kernel.OpenProcess(0x0400|0x0010,False,process.pid)
        assert handle, 'Cannot observe conversion process memory'
        try:
            while process.poll() is None:
                c=Counters();c.cb=ctypes.sizeof(c)
                if getmem(handle,ctypes.byref(c),c.cb): memory.append((time.monotonic()-start,c.private))
                if time.monotonic()-start>480:
                    process.terminate();raise RuntimeError('Sustained conversion exceeded 480 seconds')
                time.sleep(.1)
        finally: kernel.CloseHandle(handle)
        assert process.returncode==0, (work/'conversion.log').read_text(encoding='utf-8',errors='replace')
    elapsed=time.monotonic()-start
    assert memory and max(n for _,n in memory)<256*1024*1024, ('Application memory exceeded streaming budget',memory)
    report_file=max((work/'appdata/logs').glob('*.validation.json'),key=lambda p:p.stat().st_mtime)
    validation=json.loads(report_file.read_text(encoding='utf-8'))
    assert validation['validation']['passed'] and validation['processed_frames']==240
    assert validation['processing']['float_buffer_bytes']<=4*1024*1024
    assert validation['validation']['timing']['output']['cadence']['verified']
    assert validation['validation']['ffmpeg']['reference_signal']['chroma_reference_passed']
    def sample(path):
        data=run([ff,'-v','error','-i',path,'-vf',
                  r"select=eq(n\,0)+eq(n\,119)+eq(n\,239),crop=32:16:1920:1024",
                  '-fps_mode','passthrough','-pix_fmt','yuv422p10le','-f','rawvideo','pipe:1'])
        a=array.array('H');a.frombytes(data);assert len(a)==32*16*2*3;return a
    before,after=sample(source),sample(output)
    a=.17883277;b=1-4*a;c=.5-a*math.log(4*a)
    def linear(x): return x*x/3 if x<=.5 else (math.exp((x-c)/a)+b)/12
    def apple(x): return .08550479*math.log2(x+.00964052)+.69336945 if x>=.01 else 47.28711236*(x+.05641088)**2
    errors=[]
    for frame in range(3):
        for i in range(32*16):
            index=frame*32*16*2+i
            expected=64+876*apple(linear((before[index]-64)/876)/linear(.75))
            errors.append(abs(after[index]-expected))
    assert max(errors)<2, ('Post-encode temporal sample error',max(errors))
    result={'passed':True,'raster':'3840x2160','capture_fps':120,'frames':240,'media_seconds':2,
            'elapsed_seconds':elapsed,'conversion_fps':240/elapsed,'app_peak_private_bytes':max(n for _,n in memory),
            'float_buffer_bytes':validation['processing']['float_buffer_bytes'],
            'sampled_frames':[0,119,239],'max_post_encode_luma_code_error':max(errors),
            'samples':len(errors),'memory_samples':memory}
    (work/'sustained-report.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k!='memory_samples'},indent=2))


if __name__=='__main__': main()
