"""Independent integer-YCbCr boundary, tone, and saturation regression.

Fixtures and expected YCbCr codes use scalar equations, not FFmpeg zscale.
Only codec encode/decode and the production executable are exercised by FFmpeg.
"""
import argparse
import array
import json
import math
import os
from pathlib import Path
import subprocess

from integration import run


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cli', type=Path, required=True)
    ap.add_argument('--ffmpeg', type=Path, required=True)
    ap.add_argument('--work', type=Path, required=True)
    args = ap.parse_args()
    ff, cli, work = args.ffmpeg.resolve(), args.cli.resolve(), args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    env = {k.upper(): v for k, v in os.environ.items()}
    env['LOGFORGE_DATA_DIR'] = str(work/'appdata')
    run([args.cli.resolve(),'--approve-ffmpeg','--ffmpeg',ff],env=env)
    neutral = [-.03, 0, .002, .004, .008, .01, .02, .03, .05, .075, .1,
               .2, .3, .3782588830779046, .5, .65, .75, .9, 1, 1.05, 1.08]
    patches = [(v, v, v) for v in neutral] + [(.75,.2,.1), (.1,.75,.2), (.2,.1,.75),
                                              (1,0,0), (0,1,0), (0,0,1), (.1,.03,.01)]
    width, height, frames = len(patches)*32, 64, 6

    def matrix(rgb):
        r,g,b = rgb
        y = .2627*r+.678*g+.0593*b
        return 64+876*y, 512+896*(b-y)/1.8814, 512+896*(r-y)/1.4746

    def inverse_matrix(codes):
        y,cb,cr = (codes[0]-64)/876, (codes[1]-512)/896, (codes[2]-512)/896
        r,b = y+1.4746*cr, y+1.8814*cb
        return r, (y-.2627*r-.0593*b)/.678, b

    codes = [matrix(rgb) for rgb in patches]
    fixture = array.array('H')
    for plane in range(3):
        plane_width = width if plane == 0 else width//2
        for _ in range(height):
            for x in range(plane_width):
                value = round(codes[x//(32 if plane == 0 else 16)][plane])
                assert 4 <= value <= 1019
                fixture.append(value)
    raw = work/'signal-fixture.yuv'
    with raw.open('wb') as f:
        for _ in range(frames):
            fixture.tofile(f)
    source = work/'HLG-code-values.mov'
    run([ff,'-v','error','-y','-f','rawvideo','-pixel_format','yuv422p10le',
         '-video_size',f'{width}x{height}','-framerate','24','-i',raw,
         '-vf','setparams=color_primaries=bt2020:color_trc=arib-std-b67:colorspace=bt2020nc:range=limited',
         '-c:v','prores_ks','-profile:v','3','-qscale:v','1','-threads','2',
         '-color_range','tv','-color_primaries','bt2020','-color_trc','arib-std-b67',
         '-colorspace','bt2020nc','-movflags','+write_colr',source])

    def native_codes(path):
        data = run([ff,'-v','error','-noautorotate','-i',path,'-frames:v','1',
                    '-pix_fmt','yuv422p10le','-f','rawvideo','pipe:1'])
        a = array.array('H'); a.frombytes(data)
        assert len(a) == width*height*2
        return a

    def patch_codes(data, p):
        result = []
        for plane in range(3):
            pw = width if plane == 0 else width//2
            offset = 0 if plane == 0 else width*height+(plane-1)*width*height//2
            x0 = p*(32 if plane == 0 else 16)+(12 if plane == 0 else 6)
            result.append(sum(data[offset+y*pw+x] for y in range(24,40) for x in range(x0,x0+4))/64)
        return result

    a=.17883277; b=1-4*a; c=.5-a*math.log(4*a)
    def ih(v):
        return -v*v/3 if v<0 else v*v/3 if v<=.5 else (math.exp((v-c)/a)+b)/12
    def apple(v):
        if v >= .01: return .08550479*math.log2(v+.00964052)+.69336945
        if v >= -.05641088: return 47.28711236*(v+.05641088)**2
        return 0

    def reference(rgb, creative):
        linear = [ih(v)/ih(.75) for v in rgb]
        if creative:
            y = sum(k*v for k,v in zip((.2627,.678,.0593), linear))
            stops = math.log2(y/.18) if y>0 else -6
            t = min(abs(stops)/6, 1)
            gain = 2**((3 if stops<0 else -1)*t*t*(3-2*t))
            linear = [gain*(y+.85*(v-y)) for v in linear]
        return matrix([apple(v) for v in linear])

    decoded_source = native_codes(source)
    results = []
    for creative in (False, True):
        dest = work/('creative.mov' if creative else 'standard.mov')
        if dest.exists(): dest.unlink()
        extra = ['--tone'] if creative else []
        run([cli,'--convert',source,dest,'--ffmpeg',ff,'--input-chroma-location','left',*extra],env=env)
        actual = native_codes(dest)
        errors, measured = [], []
        for p,rgb in enumerate(patches):
            expected = reference(inverse_matrix(patch_codes(decoded_source,p)),creative)
            observed = patch_codes(actual,p)
            error = max(abs(e-o) for e,o in zip(expected,observed))
            assert error<3, (creative,p,expected,observed,error)
            errors.append(error); measured.append(observed)
        # Very dark 10-bit inputs can legitimately quantize to the same Apple
        # toe code. Never claim lossless conversion or demand invented detail.
        # Clearly separated shadows/highlights must retain their ordering.
        for lo,hi in ((5,7),(7,9),(9,11),(16,17),(17,18),(18,20)):
            assert measured[hi][0] > measured[lo][0]+1, ('Collapsed ramp',creative,lo,hi)
        assert measured[1][0] > 180, 'Apple black was crushed to video black'
        assert measured[20][0] < 900, 'Super-white signal was clipped to white'
        report_file = max((work/'appdata/logs').glob('LogForge-'+dest.name+'-*.validation.json'),key=lambda p:p.stat().st_mtime)
        validation = json.loads(report_file.read_text(encoding='utf-8'))
        signal = validation['validation']['signal']
        assert signal['samples'] == width*height*3*frames
        # Hard chroma boundaries can produce matrix/resampling excursions even
        # when patch centers are nominal. The counters cover these edges too.
        assert bool(signal['above_nominal_white']) == (signal['output_apple_log_max'] > 1)
        assert validation['validation']['signal_warning'] == bool(signal['above_nominal_white'] or signal['apple_floor_clipped'])
        assert validation['color']['creative_adjustment']['enabled'] == creative
        tags = validation['output']['format']['tags']
        assert tags['logforge.rendering'] == ('creative-luma-v1' if creative else 'standard')
        assert float(tags['logforge.saturation']) == (.85 if creative else 1)
        results.append({'creative':creative,'max_error_10bit_codes':max(errors),'patch_codes':measured})
    assert results[1]['patch_codes'][7][0] > results[0]['patch_codes'][7][0]+10, 'Shadows not lifted'
    assert results[1]['patch_codes'][18][0] < results[0]['patch_codes'][18][0]-30, 'Highlights not compressed'
    assert abs(results[1]['patch_codes'][13][0]-results[0]['patch_codes'][13][0])<2, 'Gray anchor changed'

    # Extreme exposure must report signal risk instead of an unqualified success.
    risk = work/'exposure-risk.mov'
    if risk.exists(): risk.unlink()
    run([cli,'--convert',source,risk,'--ffmpeg',ff,'--input-chroma-location','left','--exposure-ev','4'],env=env)
    risk_file = max((work/'appdata/logs').glob('LogForge-'+risk.name+'-*.validation.json'),key=lambda p:p.stat().st_mtime)
    risk_report = json.loads(risk_file.read_text(encoding='utf-8'))['validation']
    assert risk_report['signal_warning'] and risk_report['signal']['above_nominal_white']>0

    for parameters in (['--tone','--shadow-lift-ev','3.1'], ['--tone','--highlight-compression-ev','nan'],
                       ['--tone','--saturation-percent','151'], ['--tone','--saturation-percent','85junk'],
                       ['--shadow-lift-ev','1']):
        bad = work/'invalid-must-not-exist.mov'
        result = subprocess.run([str(x) for x in [cli,'--convert',source,bad,'--ffmpeg',ff,'--input-chroma-location','left',*parameters]],
                                 env=env,capture_output=True)
        assert result.returncode!=0 and not bad.exists(), parameters
    report = {'passed':True,'reference':'Independent scalar BT.2020 NCL and integer video-range mapping',
              'patch_count':len(patches),'cases':results,'out_of_range_warning_verified':True}
    (work/'signal-report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps(report,indent=2))


if __name__ == '__main__':
    main()
