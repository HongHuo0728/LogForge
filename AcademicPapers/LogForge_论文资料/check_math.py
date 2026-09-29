import re
import build_paper as b
from matplotlib.mathtext import MathTextParser
p=MathTextParser('path')
t=(b.ROOT/'论文正文.md').read_text(encoding='utf-8')
errors=[]
for n,ex in enumerate(re.findall(r'^\$\$(.*?)\$\$$',t,re.M),1):
    try:p.parse('$'+b.math_normalize(ex)+'$')
    except Exception as e:errors.append((n,str(e)))
print(errors)
