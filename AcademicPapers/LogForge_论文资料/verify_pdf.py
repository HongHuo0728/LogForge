from pathlib import Path
from PIL import Image, ImageDraw
import pymupdf as fitz
import json, re

root=Path(__file__).parent
pdf=root.parent/'LogForge_源码解析与数学论证_学术论文_zhcn.pdf'
doc=fitz.open(pdf)
qa=root/'版式检查';qa.mkdir(exist_ok=True)
for n,page in enumerate(doc):page.get_pixmap(matrix=fitz.Matrix(0.85,0.85)).save(qa/f'page-{n+1:03}.png')
pages=sorted(qa.glob('page-*.png'))[:len(doc)]
for start in range(0,len(pages),12):
    sheet=Image.new('RGB',(1600,2360),'#d8dee3');draw=ImageDraw.Draw(sheet)
    for j,path in enumerate(pages[start:start+12]):
        im=Image.open(path);im.thumbnail((378,550))
        x=(j%4)*400+(400-im.width)//2;y=(j//4)*780+35
        sheet.paste(im,(x,y));draw.text((x,y-22),f'PAGE {start+j+1}',fill='black')
    sheet.save(qa/f'contact_{start//12+1:02}.jpg',quality=92)
issues=[];stats=[]
for n,page in enumerate(doc):
    text=page.get_text()
    stats.append({'page':n+1,'characters':len(text),'first_text':text[:150]})
    for b in page.get_text('dict')['blocks']:
        for line in b.get('lines',[]):
            for s in line['spans']:
                x0,y0,x1,y1=s['bbox']
                if x0<0 or y0<0 or x1>page.rect.width+1 or y1>page.rect.height+1:
                    issues.append({'page':n+1,'kind':'outside_page','text':s['text'],'bbox':s['bbox']})
    if '\ufffd' in text:issues.append({'page':n+1,'kind':'replacement_character'})
    if any(t in text for t in ['@figure:','@table:','@inventory','@references','@results']):issues.append({'page':n+1,'kind':'unexpanded_directive'})
    if len(text.strip())<60:issues.append({'page':n+1,'kind':'almost_blank'})
result={'pages':len(doc),'issues':issues,'page_stats':stats,'embedded_font_names':sorted(set(f[3] for p in doc for f in p.get_fonts())),'outline_entries':len(doc.get_toc()),'pdf_bytes':pdf.stat().st_size}
(qa/'结构检查.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k!='page_stats'},ensure_ascii=False,indent=2))
for page in [0,1,5,8,11,12,18,21,25,28,32,40,45,48,54,60,63]:
    if page>=len(doc):continue
    doc[page].get_pixmap(matrix=fitz.Matrix(1.5,1.5)).save(qa/f'detail_{page+1:02}.png')
