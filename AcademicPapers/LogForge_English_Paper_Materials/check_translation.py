from pathlib import Path
import re,json,hashlib
root=Path(__file__).parent
zhroot=root.parent/'LogForge_论文资料'
zh=(zhroot/'论文正文.md').read_text(encoding='utf-8')
en=(root/'manuscript_en.md').read_text(encoding='utf-8')
eq=lambda s:re.findall(r'^\$\$(.*)\$\$$',s,re.M)
fig=lambda s:re.findall(r'^@figure:([^|]+)',s,re.M)
sub=lambda s:re.findall(r'^## ([\dA-Z]+\.\d+)',s,re.M)
blocks=lambda s:[l for l in s.splitlines() if l.strip()]
orig=blocks(zh);tran=blocks(en)
problems=[]
if eq(zh)!=eq(en):
    for i,(a,b) in enumerate(zip(eq(zh),eq(en)),1):
        if a!=b:problems.append({'equation':i,'zh':a,'en':b})
    if len(eq(zh))!=len(eq(en)):problems.append('Equation count mismatch')
if fig(zh)!=fig(en):problems.append('Figure IDs or sequence differ')
if sub(zh)!=sub(en):problems.append('Subsection identifiers differ')
if len(orig)!=len(tran):problems.append({'nonempty_lines_zh':len(orig),'nonempty_lines_en':len(tran)})
for i,(a,b) in enumerate(zip(orig,tran),1):
    # Compare all numerical literals in prose and tables; tolerate numbers translated from Chinese words.
    if a.startswith(('#','@','$$')):continue
    nums=re.findall(r'(?<![A-Za-z])\d+(?:\.\d+)?',a)
    missing=[n for n in nums if n not in b]
    if missing:problems.append({'block':i,'missing_literals':missing,'zh':a,'en':b})
report={'source_commit':'913e4b9417fa0f32b88629c39062b54589d9dd21','equations':len(eq(en)),'figures':len(fig(en)),'subsections':len(sub(en)),'nonempty_blocks':len(tran),'english_words':len(re.findall(r"\b[A-Za-z]+(?:[-'][A-Za-z]+)*\b",en)),'chinese_characters_in_english':len(re.findall('[\u4e00-\u9fff]',en)),'problems':problems}
(root/'translation_checks.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=False,indent=2))
