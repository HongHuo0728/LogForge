from pathlib import Path
from PIL import Image, ImageDraw
import pymupdf as fitz
import hashlib,json,re
root=Path(__file__).parent
title='LogForge Source Code Analysis and Mathematical Proof - Academic Paper'
pdf=root.parent/(title+'.pdf')
qa=root/'qa';qa.mkdir(exist_ok=True)
doc=fitz.open(pdf)
pages=sorted(qa.glob('page-*.png'))[:len(doc)]
for start in range(0,len(pages),12):
    sheet=Image.new('RGB',(1600,1820),'#d8dee3');d=ImageDraw.Draw(sheet)
    for j,path in enumerate(pages[start:start+12]):
        im=Image.open(path);im.thumbnail((378,551))
        x=(j%4)*400+(400-im.width)//2;y=(j//4)*605+30
        sheet.paste(im,(x,y));d.text((x,y-21),f'PAGE {start+j+1}',fill='black')
    sheet.save(qa/f'contact_{start//12+1:02}.jpg',quality=92)
issues=[];stats=[];texts=[]
for n,page in enumerate(doc):
    text=page.get_text();texts.append(text)
    stats.append({'page':n+1,'characters':len(text),'first_text':text[:240]})
    for b in page.get_text('dict')['blocks']:
        for line in b.get('lines',[]):
            for s in line['spans']:
                x0,y0,x1,y1=s['bbox']
                if x0<0 or y0<0 or x1>page.rect.width+1 or y1>page.rect.height+1:issues.append({'page':n+1,'kind':'outside_page','text':s['text'],'bbox':s['bbox']})
    if '\ufffd' in text:issues.append({'page':n+1,'kind':'replacement_character'})
    if any(t in text for t in ['@figure:','@table:','@inventory','@references','@results']):issues.append({'page':n+1,'kind':'unexpanded_directive'})
    if len(text.strip())<60:issues.append({'page':n+1,'kind':'almost_blank'})
    if re.search('[\u4e00-\u9fff]',text):issues.append({'page':n+1,'kind':'untranslated_chinese'})
original=json.loads((root/'original_chinese_files_sha256.json').read_text(encoding='utf-8-sig'))
changed=[]
for item in original:
    path=Path(item['Path'])
    if not path.exists() or hashlib.sha256(path.read_bytes()).hexdigest().upper()!=item['SHA256']:changed.append(str(path))
zhpdf=root.parent/'LogForge_源码解析与数学论证_学术论文.pdf'
zhhash=hashlib.sha256(zhpdf.read_bytes()).hexdigest()
if zhhash!='f4f3277e50b780f89f1dafa876ec3b9052db9a5718745579527e48794140b99e':changed.append(str(zhpdf))
assets=[]
for path in (root/'figures').iterdir():
    src=root.parent/'LogForge_论文资料/figures'/path.name
    if hashlib.sha256(path.read_bytes()).digest()!=hashlib.sha256(src.read_bytes()).digest():assets.append(path.name)
result={'pages':len(doc),'issues':issues,'chinese_files_checked':len(original)+1,'changed_chinese_files':changed,'chinese_pdf_sha256':zhhash,'changed_figure_assets':assets,'english_pdf_sha256':hashlib.sha256(pdf.read_bytes()).hexdigest(),'page_stats':stats,'font_names':sorted(set(f[3] for page in doc for f in page.get_fonts())),'outline_entries':len(doc.get_toc()),'pdf_bytes':pdf.stat().st_size}
(qa/'verification.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
(qa/'extracted_text.txt').write_text('\n\f\n'.join(texts),encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k!='page_stats'},indent=2))
toc=doc.get_toc()
selected={1,2,len(doc)}
for _,heading,pagenum in toc:
    if heading.startswith(('Chapter 4.','Chapter 6.','Chapter 9.','Chapter 15.','Chapter 21.','Appendix B.')):
        selected.add(pagenum);selected.add(min(len(doc),pagenum+1))
for page in sorted(selected):doc[page-1].get_pixmap(matrix=fitz.Matrix(1.6,1.6)).save(qa/f'detail_{page:02}.png')
