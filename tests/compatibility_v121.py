"""Two small generated MOV regressions; no camera originals are required."""
import argparse
import array
from fractions import Fraction
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cli', type=Path, required=True)
    ap.add_argument('--ffmpeg', type=Path, required=True)
    ap.add_argument('--work', type=Path, required=True)
    ap.add_argument('--case', choices=['iphone', 'blackmagic'], required=True)
    args = ap.parse_args()
    root = args.work.resolve() / args.case
    root.mkdir(parents=True, exist_ok=True)
    env = {k.upper(): v for k, v in os.environ.items()}
    env['LOGFORGE_DATA_DIR'] = str(root / 'appdata')
    cli, ff = args.cli.resolve(), args.ffmpeg.resolve()
    probe = ff.with_name('ffprobe.exe')

    def run(command):
        p = subprocess.run([str(x) for x in command], env=env, capture_output=True, timeout=60)
        assert p.returncode == 0, p.stderr.decode('utf-8', errors='replace') + p.stdout.decode('utf-8', errors='replace')
        return p.stdout

    def info(path):
        return json.loads(run([probe, '-v', 'error', '-show_format', '-show_streams', '-of', 'json', path]))

    # Independent minimal fixture reader: documented box lengths, not application
    # parser output, establish exactly which tkhd and elst bytes are tested.
    def boxes(data, start=0, end=None, parent=''):
        end = len(data) if end is None else end
        at, counts = start, {}
        while at < end:
            size, kind = struct.unpack_from('>I4s', data, at)
            header = 8
            if size == 1:
                size, header = struct.unpack_from('>Q', data, at + 8)[0], 16
            if size == 0:
                size = end - at
            assert size >= header and at + size <= end
            kind = kind.decode('ascii'); n = counts.get(kind, 0); counts[kind] = n + 1
            path = f'{parent}/{kind}[{n}]'
            yield kind, path, at + header, at + size
            if kind in ('moov', 'trak', 'mdia', 'edts'):
                yield from boxes(data, at + header, at + size, path)
            at += size
        assert at == end

    def headers(path):
        data = path.read_bytes(); result = {}
        for kind, name, at, end in boxes(data):
            if kind in ('mvhd', 'tkhd', 'mdhd'):
                width = 8 if data[at] == 1 else 4
                result[name] = {'creation': int.from_bytes(data[at+4:at+4+width], 'big')}
                if kind == 'tkhd':
                    pos = at + (52 if data[at] == 1 else 40)
                    result[name]['matrix'] = list(struct.unpack_from('>9i', data, pos))
        return result

    run([cli, '--approve-ffmpeg', '--ffmpeg', ff])
    source, output = root / 'source.mov', root / 'output.mov'
    delayed = args.case == 'blackmagic'
    command = [ff, '-v', 'error', '-y', '-copyts', '-f', 'lavfi', '-i', 'testsrc2=size=128x64:rate=24']
    if delayed:
        command += ['-itsoffset', '0.0104166666667']
    command += ['-f', 'lavfi', '-i', 'sine=sample_rate=48000:duration=0.5', '-map', '0:v:0', '-map', '1:a:0',
                '-frames:v', '12', '-vf', "format=yuv422p10le,geq=lum='if(lt(X,W/2),if(lt(Y,H/2),128,512),if(lt(Y,H/2),320,768))':cb=512:cr=512,setparams=color_primaries=bt2020:color_trc=arib-std-b67:colorspace=bt2020nc:range=limited", '-c:v', 'prores_ks', '-profile:v', '2' if delayed else '3',
                '-threads:v', '2', '-color_primaries', 'bt2020', '-color_trc', 'arib-std-b67', '-colorspace', 'bt2020nc',
                '-color_range', 'tv', '-c:a', 'pcm_s24le', '-ac', '2', '-timecode', '01:02:03:04',
                '-metadata', 'creation_time=2024-05-01T12:00:00Z', '-metadata', 'com.apple.quicktime.creationdate=2024-05-01T20:00:00.123+0800',
                '-movflags', '+write_colr+use_metadata_tags', '-movie_timescale', '48000', source]
    run(command)
    data = bytearray(source.read_bytes())
    all_boxes = list(boxes(data))
    # The iPhone fixture uses the exact -180 unit rotation with width/height
    # translation. Both are now applied to pixels, leaving identity orientation.
    matrix = [-65536, 0, 0, 0, -65536, 0, 128*65536, 64*65536, 1 << 30] if not delayed else [0, -65536, 0, 65536, 0, 0, 0, 0, 1 << 30]
    for index, (kind, path, at, end) in enumerate(all_boxes):
        if kind in ('tkhd', 'mdhd'):
            width = 8 if data[at] == 1 else 4
            timestamp = int.from_bytes(data[at+4:at+4+width], 'big') + index
            data[at+4:at+4+width] = timestamp.to_bytes(width, 'big')
        if kind == 'tkhd' and '/trak[0]/' in path:
            struct.pack_into('>9i', data, at + (52 if data[at] == 1 else 40), *matrix)
    source.write_bytes(data)
    before = info(source)
    video, audio = [next(s for s in before['streams'] if s['codec_type'] == t) for t in ('video', 'audio')]
    assert video['start_pts'] == 0
    if delayed:
        assert Fraction(audio['start_pts']) * Fraction(audio['time_base']) == Fraction(500, 48000)
        atom = next(b for b in all_boxes if b[0] == 'elst' and '/trak[1]/' in b[1])
        at = atom[2]
        assert data[at] == 0 and struct.unpack_from('>I', data, at + 4)[0] == 2
        assert struct.unpack_from('>Iihh', data, at + 8) == (500, -1, 1, 0)
        assert struct.unpack_from('>Iihh', data, at + 20)[1:] == (0, 1, 0)
    else:
        assert next(s['rotation'] for s in video['side_data_list'] if 'rotation' in s) == -180

    output.unlink(missing_ok=True)
    run([cli, '--convert', source, output, '--ffmpeg', ff, '--input-chroma-location', 'left', '--backend', 'cpu'])
    after = info(output)
    out_video, out_audio = [next(s for s in after['streams'] if s['codec_type'] == t) for t in ('video', 'audio')]
    original_headers, output_headers = headers(source), headers(output)
    identity = [65536, 0, 0, 0, 65536, 0, 0, 0, 1 << 30]
    for name, header in original_headers.items():
        if '/trak[0]/' in name and 'matrix' in header:
            header['matrix'] = identity
    assert original_headers == output_headers, 'Creation fields or normalized matrix incorrect'
    width, height = (64, 128) if delayed else (128, 64)
    assert (out_video['width'], out_video['height']) == (width, height)
    assert all(s.get('rotation', 0) == 0 for s in out_video.get('side_data_list', []))
    def corners(path, auto):
        command = [ff, '-v', 'error'] + ([] if auto else ['-noautorotate'])
        raw = run(command + ['-i', path, '-map', '0:v:0', '-frames:v', '1', '-pix_fmt', 'yuv422p10le', '-f', 'rawvideo', 'pipe:1'])
        values = array.array('H'); values.frombytes(raw[:width*height*2])
        return [values[y*width+x] for y in (height//4, 3*height//4) for x in (width//4, 3*width//4)]
    source_corners, output_corners = corners(source, True), corners(output, False)
    assert sorted(range(4), key=source_corners.__getitem__) == sorted(range(4), key=output_corners.__getitem__), 'Pixel orientation disagrees with source playback'
    assert out_video['tags']['timecode'] == video['tags']['timecode']
    assert out_audio['codec_name'] == audio['codec_name'] == 'pcm_s24le'
    assert out_audio['sample_rate'] == '48000' and out_audio['channels'] == 2
    offset = lambda v, a: Fraction(a['start_pts']) * Fraction(a['time_base']) - Fraction(v['start_pts']) * Fraction(v['time_base'])
    assert offset(video, audio) == offset(out_video, out_audio), 'Audio/video relative start offset changed'
    audio_bytes = lambda p: run([ff, '-v', 'error', '-i', p, '-map', '0:a:0', '-c:a', 'copy', '-f', 's24le', 'pipe:1'])
    assert hashlib.sha256(audio_bytes(source)).digest() == hashlib.sha256(audio_bytes(output)).digest(), 'Audio stream-copy payload changed'
    assert before['format']['tags']['creation_time'] == after['format']['tags']['creation_time']
    assert before['format']['tags']['com.apple.quicktime.creationdate'] == after['format']['tags']['com.apple.quicktime.creationdate']
    report = {'case': args.case, 'passed': True, 'rotation_applied_to_pixels': True,
              'orientation_matches_source_playback': True, 'output_raster': [width, height],
              'creation_headers_preserved': True, 'timecode_preserved': True, 'pcm_payload_identical': True,
              'audio_offset_seconds': float(offset(out_video, out_audio))}
    (root / 'result.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report))
    source.unlink(); output.unlink()


if __name__ == '__main__':
    main()
