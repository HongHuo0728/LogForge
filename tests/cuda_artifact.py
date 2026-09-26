"""Fail if embedded PTX was not rebuilt after modifying its CUDA source."""
import hashlib
import json
from pathlib import Path
import re

root=Path(__file__).resolve().parents[1]
manifest=json.loads((root/'src/color/ColorKernel.manifest.json').read_text(encoding='utf-8'))
source=(root/'src/color/ColorKernel.cu').read_text(encoding='utf-8').encode()
assert hashlib.sha256(source).hexdigest()==manifest['source_sha256'], 'CUDA source/PTX manifest mismatch'
header=(root/'src/color/ColorKernel.ptx.h').read_text(encoding='utf-8')
data=bytes(int(n) for n in re.findall(r'\d+',header.split('[]={',1)[1].split('};',1)[0]))
assert data[-1]==0 and b'.target sm_75' in data
assert hashlib.sha256(data[:-1]).hexdigest()==manifest['ptx_sha256'], 'Embedded PTX hash mismatch'
for flag in ['--fmad=false','--ftz=false','--prec-div=true','--prec-sqrt=true']:
    assert flag in manifest['options']
assert '--use_fast_math' not in manifest['options']
print('PASS: CUDA source, embedded PTX, target and precise compiler options')
