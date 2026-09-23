"""Real codec/pixel integration test. Python standard library only; no media assets."""
import argparse
import array
import json
import math
import os
import pathlib
import subprocess
import sys


def run(args, **kw):
    result = subprocess.run([str(a) for a in args], capture_output=True, check=False, **kw)
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}): {args}\n{result.stderr.decode('utf-8', 'replace')}\n{result.stdout.decode('utf-8', 'replace')}")
    return result.stdout


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cli', required=True, type=pathlib.Path)
    ap.add_argument('--ffmpeg', required=True, type=pathlib.Path)
    ap.add_argument('--work', required=True, type=pathlib.Path)
    args = ap.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    ff = args.ffmpeg.resolve()
    probe = ff.with_name('ffprobe.exe')
    env = {k.upper(): v for k, v in os.environ.items()}
    env['LOGFORGE_DATA_DIR'] = str(work / 'appdata')
    run([args.cli.resolve(),'--approve-ffmpeg','--ffmpeg',ff],env=env)
    width, height, frames = 320, 180, 60
    source, output = work / 'HLG 测试 input.mov', work / 'Apple Log output.mOv'
    raw = work / 'patterns.gbrpf32le'
    pixels = array.array('f')
    for channel in (1, 2, 0):  # GBR planar
        for y in range(height):
            for x in range(width):
                t = x / (width - 1)
                if y < 36:
                    value = t
                elif y < 72:
                    value = t * 0.1
                elif y < 108:
                    value = 0.5 + t * 0.57  # highlights including super-white
                elif y < 144:
                    patch = min(7, x // 40)
                    colors = [(0,0,0),(.75,.75,.75),(1,1,1),(1,0,0),(0,1,0),(0,0,1),(1,1,0),(0,1,1)]
                    value = colors[patch][channel]
                else:
                    value = (t, 1-t, 0.5)[channel]
                pixels.append(value)
    with raw.open('wb') as f:
        for _ in range(frames):
            pixels.tofile(f)
    matrix = 'zscale=matrixin=gbr:matrix=2020_ncl:rangein=full:range=limited:transferin=linear:transfer=linear:primariesin=2020:primaries=2020:chromal=left:dither=error_diffusion,format=yuv422p10le,setparams=color_primaries=bt2020:color_trc=arib-std-b67:colorspace=bt2020nc:range=limited'
    run([ff,'-v','error','-y','-f','rawvideo','-pixel_format','gbrpf32le','-video_size',f'{width}x{height}','-framerate','30000/1001','-i',raw,
         '-f','lavfi','-i','sine=frequency=997:sample_rate=48000:duration=2.002','-map','0:v','-map','1:a',
         '-vf',matrix,'-c:v','prores_ks','-profile:v','3','-threads:v','2','-color_primaries','bt2020','-color_trc','arib-std-b67','-colorspace','bt2020nc','-color_range','tv',
         '-c:a','pcm_s24le','-ac','2','-timecode','10:20:30:00','-metadata','creation_time=2024-05-01T12:00:00Z',
         '-metadata','com.apple.quicktime.make=Apple','-metadata','com.apple.quicktime.model=iPhone 13 Pro','-movflags','+write_colr+use_metadata_tags',source])
    if output.exists():
        output.unlink()  # only the exact generated test output
    converted = run([args.cli.resolve(),'--convert',source,output,'--ffmpeg',ff,'--input-chroma-location','left'], env=env)
    print(converted.decode('utf-8','replace'))
    info = json.loads(run([probe,'-v','error','-show_format','-show_streams','-of','json',output]))
    video = next(s for s in info['streams'] if s['codec_type']=='video')
    assert video['codec_name']=='prores' and video['profile']=='HQ'
    assert video['pix_fmt']=='yuv422p10le' and int(video['nb_frames'])==frames
    assert video['color_primaries']=='bt2020' and video['color_space']=='bt2020nc'
    atoms = json.loads(run([args.cli.resolve(), '--analyze', output, '--ffmpeg', ff], env=env))
    logs = [a for a in atoms['atoms'] if a['type'] == 'logs']
    assert len(logs) == 1 and logs[0]['log_transfer_function'] == 'com.apple.rec2020.apple-log'
    assert '/apch[0]/logs[0]' in logs[0]['path'] and logs[0]['size'] == 35
    assert info['format']['tags']['com.apple.quicktime.model']=='iPhone 13 Pro'
    assert video['tags']['timecode']=='10:20:30:00'
    def decode(path):
        data = run([ff,'-v','error','-noautorotate','-i',path,'-map','0:v:0','-frames:v','1','-vf',
            'zscale=matrixin=2020_ncl:matrix=gbr:rangein=limited:range=full:transferin=linear:transfer=linear:primariesin=2020:primaries=2020:filter=spline36,format=gbrpf32le',
            '-f','rawvideo','-pix_fmt','gbrpf32le','pipe:1'])
        values = array.array('f'); values.frombytes(data); return values
    before, after = decode(source), decode(output)
    a=.17883277; b=1-4*a; c=.5-a*math.log(4*a)
    def inverse_hlg(v):
        return -v*v/3 if v<0 else v*v/3 if v<=.5 else (math.exp((v-c)/a)+b)/12
    scale=1/inverse_hlg(.75)
    def apple(r):
        return .08550479*math.log2(r+.00964052)+.69336945 if r>=.01 else 47.28711236*(r+.05641088)**2 if r>=-.05641088 else 0
    errors, changes = [], []
    for plane in range(3):
        for y in range(4,height-4):
            if min(y%36,35-y%36)<4: continue
            for x in range(6,width-6):
                if 108<=y<144 and min(x%40,39-x%40)<6: continue
                i=plane*width*height+y*width+x
                errors.append(abs(after[i]-apple(inverse_hlg(before[i])*scale)))
                changes.append(abs(after[i]-before[i]))
    mae=sum(errors)/len(errors); maximum=max(errors)
    assert mae<.003, ('Pixel mean absolute error',mae)
    assert maximum<.025, ('Pixel peak absolute error away from discontinuities',maximum)
    assert sum(changes)/len(changes)>.06, 'Pixels did not change enough: possible metadata-only conversion'
    assert info['format']['tags']['logforge.reference']=='BT2408_HLG75pct_to_100pct_reflectance'
    assert float(info['format']['tags']['logforge.exposure_ev'])==0
    # Independently verify the user exposure option in actual codec pixels.
    exposure_output=work/'exposure-plus1.mov'
    if exposure_output.exists():exposure_output.unlink()
    run([args.cli.resolve(),'--convert',source,exposure_output,'--ffmpeg',ff,'--input-chroma-location','left','--exposure-ev','1'],env=env)
    exposed=decode(exposure_output)
    exposure_errors=[]
    for plane in range(3):
        for y in (16,52,88):  # neutral ramps, well clear of chroma discontinuities
            for x in range(6,width-6):
                i=plane*width*height+y*width+x
                exposure_errors.append(abs(exposed[i]-apple(inverse_hlg(before[i])*scale*2)))
    assert sum(exposure_errors)/len(exposure_errors)<.003
    exposure_info=json.loads(run([probe,'-v','error','-show_format','-of','json',exposure_output]))
    assert float(exposure_info['format']['tags']['logforge.exposure_ev'])==1
    for invalid_exposure in ('nan','9','1oops'):
        bad_exposure=work/'bad-exposure-must-not-exist.mov'
        rejected_exposure=subprocess.run([str(args.cli.resolve()),'--convert',str(source),str(bad_exposure),
                                          '--ffmpeg',str(ff),'--input-chroma-location','left','--exposure-ev',invalid_exposure],env=env,capture_output=True)
        assert rejected_exposure.returncode!=0 and not bad_exposure.exists(), 'Invalid exposure accepted'
    hashes=[]
    for p in (source,output):
        hashes.append(run([ff,'-v','error','-i',p,'-map','0:a:0','-c','copy','-f','hash','-hash','sha256','-']).strip())
    assert hashes[0]==hashes[1], 'Audio payload changed'
    # A camera can supply two unlabelled PCM channels. With FFmpeg's default
    # layout guessing, stream copy invents "stereo" and the strict validator fails.
    # Generate this case without retaining any private camera media in the repo.
    pcm16=work/'pcm16-labelled.mov'
    run([ff,'-v','error','-y','-i',source,'-map','0:v:0','-map','0:a:0','-c:v','copy','-c:a','pcm_s16le',pcm16])
    audio_layout_cases=[]
    for rotation in (0,90):
        unknown=work/f'pcm16-unlabelled-{rotation}.mov'
        target=work/f'pcm16-unlabelled-{rotation}-AppleLog.mov'
        run([ff,'-v','error','-y','-ch_layout:a','2C','-guess_layout_max','0',
             '-display_rotation:v:0',str(rotation),'-i',pcm16,'-map','0:v:0','-map','0:a:0','-c','copy',unknown])
        unknown_info=json.loads(run([probe,'-v','error','-select_streams','a','-show_streams','-of','json',unknown]))['streams'][0]
        assert not unknown_info.get('channel_layout'), 'Regression fixture must have no layout declaration'
        assert unknown_info['codec_name']=='pcm_s16le' and unknown_info['channels']==2
        if target.exists():target.unlink()
        run([args.cli.resolve(),'--convert',unknown,target,'--ffmpeg',ff,'--input-chroma-location','left'],env=env)
        target_info=json.loads(run([probe,'-v','error','-select_streams','a','-show_streams','-of','json',target]))['streams'][0]
        assert not target_info.get('channel_layout'), 'A layout was invented for unlabelled channels'
        for key in ('codec_name','channels','sample_rate','bits_per_sample'):
            assert target_info[key]==unknown_info[key], f'Unlabelled audio {key} changed'
        payload=[run([ff,'-v','error','-guess_layout_max','0','-i',p,'-map','0:a:0','-c','copy','-f','hash','-hash','sha256','-']).strip()
                 for p in (unknown,target)]
        assert payload[0]==payload[1], 'Unlabelled audio payload changed'
        audio_layout_cases.append({'rotation':rotation,'payload_sha256':payload[0].decode(),'layout_preserved':True})
    bad = work/'unsupported-rec709.mov'
    run([ff,'-v','error','-y','-i',source,'-map','0','-c','copy','-color_primaries','bt709','-movflags','+write_colr',bad])
    rejected=subprocess.run([str(args.cli.resolve()),'--convert',str(bad),str(work/'must-not-exist.mov'),'--ffmpeg',str(ff),'--input-chroma-location','left'],env=env,capture_output=True)
    assert rejected.returncode!=0 and not (work/'must-not-exist.mov').exists(), 'Unsupported input accepted'
    # Original image orientation, codec Standard, silent footage, and AAC are separate cases.
    variants = []
    for name, options in [
        ('rotation-90', ['-display_rotation:v:0','90']),
        ('rotation-minus90', ['-display_rotation:v:0','-90']),
        ('rotation-180', ['-display_rotation:v:0','180']),
        ('rotation-270', ['-display_rotation:v:0','270']),
        ('silent', []),
        ('aac', []),
        ('standard', []),
    ]:
        variant=work/f'{name}.mov';target=work/f'{name}-AppleLog.mov'
        cmd=[ff,'-v','error','-y',*options,'-i',source,'-map','0:v:0']
        if name!='silent': cmd+=['-map','0:a?']
        cmd+=['-c','copy']
        if name=='aac':cmd+=['-c:a','aac','-b:a','192k']
        if name=='standard':cmd+=['-c:v','prores_ks','-profile:v','2','-pix_fmt','yuv422p10le']
        cmd+=['-movflags','+write_colr+use_metadata_tags',variant]
        run(cmd)
        if target.exists():target.unlink()
        run([args.cli.resolve(),'--convert',variant,target,'--ffmpeg',ff,'--input-chroma-location','left'],env=env)
        variants.append(name)
    # Two independently mapped audio streams and real drop-frame punctuation.
    multi=work/'multi-audio-drop-frame.mov';multi_out=work/'multi-audio-drop-frame-AppleLog.mov'
    run([ff,'-v','error','-y','-i',source,'-map','0:v:0','-map','0:a:0','-map','0:a:0','-c','copy',
         '-timecode','01:23:45;12','-metadata:s:a:0','language=eng','-metadata:s:a:1','language=zho',
         '-metadata','com.apple.proapps.customgamma=HLG','-metadata','HDR_transfer=PQ',
         '-metadata','unknown_private=should be removed','-movflags','+use_metadata_tags',multi])
    if multi_out.exists():multi_out.unlink()
    run([args.cli.resolve(),'--convert',multi,multi_out,'--ffmpeg',ff,'--input-chroma-location','left'],env=env)
    multi_info=json.loads(run([probe,'-v','error','-show_streams','-show_format','-of','json',multi_out]))
    assert [s['tags']['timecode'] for s in multi_info['streams'] if s['codec_type']=='video']==['01:23:45;12']
    assert len([s for s in multi_info['streams'] if s['codec_type']=='audio'])==2
    for i in range(2):
        assert run([ff,'-v','error','-i',multi,'-map',f'0:a:{i}','-c','copy','-f','hash','-hash','sha256','-'])==run(
            [ff,'-v','error','-i',multi_out,'-map',f'0:a:{i}','-c','copy','-f','hash','-hash','sha256','-'])
    assert not {'com.apple.proapps.customgamma','HDR_transfer','unknown_private'} & multi_info['format']['tags'].keys()
    variants.append('multi-audio-drop-frame-whitelist')
    for rate in ('2999/100','2998/100','299701/10000'):
        name=rate.replace('/','-');exact=work/f'cfr-{name}.mov';target=work/f'cfr-{name}-AppleLog.mov'
        run([ff,'-v','error','-y','-f','lavfi','-i',f'testsrc2=size=128x64:rate={rate}',
             '-frames:v','90','-vf','format=yuv422p10le,setparams=color_primaries=bt2020:color_trc=arib-std-b67:colorspace=bt2020nc:range=limited',
             '-c:v','prores_ks','-profile:v','3','-pix_fmt','yuv422p10le',
             '-color_primaries','bt2020','-color_trc','arib-std-b67','-colorspace','bt2020nc',exact])
        if target.exists():target.unlink()
        run([args.cli.resolve(),'--convert',exact,target,'--ffmpeg',ff,'--input-chroma-location','left'],env=env)
        after=json.loads(run([probe,'-v','error','-show_streams','-of','json',target]))['streams'][0]
        from fractions import Fraction
        assert Fraction(after['avg_frame_rate'])==Fraction(rate) and int(after['nb_frames'])==90
        variants.append(f'exact-CFR-{rate}')
    # Full 4K raster through the production float bridge, three real ProRes frames.
    four_k=work/'4k-HLG.mov';four_k_out=work/'4k-AppleLog.mov'
    run([ff,'-v','error','-y','-i',source,'-an','-frames:v','3','-vf','zscale=w=3840:h=2160,format=yuv422p10le',
         '-c:v','prores_ks','-profile:v','3','-threads:v','4',four_k])
    if four_k_out.exists():four_k_out.unlink()
    run([args.cli.resolve(),'--convert',four_k,four_k_out,'--ffmpeg',ff,'--input-chroma-location','left'],env=env)
    variants.append('3840x2160-3frames')
    # A single 1/480-second camera clock correction must not turn 24 fps into
    # an arbitrary rational mean rate. Sustained clock drift is still refused.
    clock=work/'camera-clock.mov';clock_out=work/'camera-clock-AppleLog.mov'
    run([ff,'-v','error','-y','-stream_loop','1','-i',source,'-map','0:v:0','-an',
         '-vf',r'fps=24,settb=1/480,setpts=N*20-gte(N\,30)',
         '-enc_time_base','1:480','-fps_mode','passthrough','-video_track_timescale','480',
         '-c:v','prores_ks','-profile:v','3','-pix_fmt','yuv422p10le',clock])
    clock_info=json.loads(run([probe,'-v','error','-select_streams','v','-show_streams','-of','json',clock]))['streams'][0]
    assert clock_info['r_frame_rate']=='24/1' and clock_info['avg_frame_rate']!='24/1'
    if clock_out.exists():clock_out.unlink()
    run([args.cli.resolve(),'--convert',clock,clock_out,'--ffmpeg',ff,'--input-chroma-location','left'],env=env)
    clock_after=json.loads(run([probe,'-v','error','-select_streams','v','-show_streams','-of','json',clock_out]))['streams'][0]
    assert clock_after['avg_frame_rate']=='24/1' and clock_after['nb_frames']==clock_info['nb_frames']
    variants.append('camera-clock-correction-24fps')
    drift=work/'cumulative-drift.mov';drift_out=work/'drift-must-not-exist.mov'
    run([ff,'-v','error','-y','-stream_loop','3','-i',source,'-map','0:v:0','-an',
         '-vf','fps=24,settb=1/480,setpts=N*20-floor(N/64)',
         '-enc_time_base','1:480','-fps_mode','passthrough','-video_track_timescale','480',
         '-c:v','prores_ks','-profile:v','3','-pix_fmt','yuv422p10le',drift])
    # Metadata alone looks close enough; rejection must come from packet phase.
    run([args.cli.resolve(),'--probe',drift,'--ffmpeg',ff,'--input-chroma-location','left'],env=env)
    drift_refused=subprocess.run([str(args.cli.resolve()),'--convert',str(drift),str(drift_out),'--ffmpeg',str(ff),'--input-chroma-location','left'],env=env,capture_output=True)
    assert drift_refused.returncode!=0 and not drift_out.exists(), 'Accumulated timestamp drift accepted'
    assert b'InputCadence' in drift_refused.stderr, 'Drift was not refused by packet validation'
    # A VFR file with deliberate gaps must be refused even if its color tags are valid.
    vfr=work/'vfr.mov'
    run([ff,'-v','error','-y','-i',source,'-map','0:v:0','-vf',"setpts=PTS+floor(N/10)*1001",'-fps_mode','vfr',
         '-c:v','prores_ks','-profile:v','2','-pix_fmt','yuv422p10le','-color_primaries','bt2020','-color_trc','arib-std-b67','-colorspace','bt2020nc',vfr])
    refused=subprocess.run([str(args.cli.resolve()),'--convert',str(vfr),str(work/'vfr-must-not-exist.mov'),'--ffmpeg',str(ff),'--input-chroma-location','left'],env=env,capture_output=True)
    assert refused.returncode!=0 and not (work/'vfr-must-not-exist.mov').exists(), 'VFR accepted'
    # Overwrite is refused, and a requested cancellation cannot publish a partial movie.
    original_output_hash=__import__('hashlib').sha256(output.read_bytes()).hexdigest()
    overwrite=subprocess.run([str(args.cli.resolve()),'--convert',str(source),str(output),'--ffmpeg',str(ff),'--input-chroma-location','left'],env=env,capture_output=True)
    assert overwrite.returncode!=0 and __import__('hashlib').sha256(output.read_bytes()).hexdigest()==original_output_hash
    cancelled_output=work/'cancelled.mov'
    cancellation=subprocess.run([str(args.cli.resolve()),'--convert',str(source),str(cancelled_output),'--ffmpeg',str(ff),'--input-chroma-location','left','--cancel-after-frames','1'],env=env,capture_output=True,timeout=20)
    assert cancellation.returncode==130 and not cancelled_output.exists(), 'Cancellation published output'
    assert not list(work.glob('*.partial.mov')), 'Incomplete files were not cleaned up'
    report={'passed':True,'frames':frames,'pixel_mae':mae,'pixel_max_error':maximum,'samples':len(errors),'audio_sha256':hashes[0].decode(),'unlabelled_audio':audio_layout_cases,'variants':variants,'vfr_rejected':True,'overwrite_refused':True,'cancel_cleanup':True,'output':str(output),'source':str(source)}
    (work/'integration-report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps(report,indent=2))

if __name__=='__main__':
    main()
