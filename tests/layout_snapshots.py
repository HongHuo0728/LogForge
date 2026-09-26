"""Check real Win32 render snapshots, not a separate test-only layout."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib

work=Path(sys.argv[1])
matrix=json.loads((work/'layout-matrix.json').read_text(encoding='utf-8'))
assert len(matrix)==960 and all(c['passed'] for c in matrix)
report=json.loads((work/'ui-test.json').read_text(encoding='utf-8'))
assert report['passed'] and report['reliability_checks']['layout_gdi_stable_after_warmup']
hashes=set()
for dpi in (96,120,144,168,192):
    for language in ('en','zh'):
        for theme,color in [('dark',(17,21,27)),('light',(243,247,250))]:
            path=work/f'{dpi}-{language}-{theme}.png'
            data=path.read_bytes();assert data[:8]==b'\x89PNG\r\n\x1a\n'
            digest=hashlib.sha256(data).hexdigest()
            assert digest not in hashes, f'Stale duplicate snapshot: {path}'
            hashes.add(digest)
            at=8;compressed=b''
            while at<len(data):
                length=struct.unpack_from('>I',data,at)[0];kind=data[at+4:at+8];payload=data[at+8:at+8+length]
                if kind==b'IHDR':
                    width,height,depth,channels,_,_,interlace=struct.unpack('>IIBBBBB',payload)
                    assert depth==8 and channels in (2,6) and interlace==0
                    assert width>=1200 and height>=650
                if kind==b'IDAT':compressed+=payload
                at+=12+length
            # First pixel has zero left/up predictors for all PNG filter modes.
            pixels=zlib.decompress(compressed)
            assert tuple(pixels[1:4])==color, f'Wrong rendered theme: {path}'
print('PASS: 960 production layouts; 20 distinct theme/language/DPI snapshots; stable GDI')
