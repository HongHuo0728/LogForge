from pathlib import Path
import ast, hashlib, json, re, shutil, xml.etree.ElementTree as ET
from xml.sax.saxutils import escape
import pymupdf as fitz
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.enums import TA_JUSTIFY
from reportlab.lib.pagesizes import A4
from reportlab.platypus import BaseDocTemplate, PageTemplate, Frame, Paragraph, Spacer, PageBreak, Table, TableStyle, KeepTogether, Flowable, NextPageTemplate
from reportlab.platypus.tableofcontents import TableOfContents
from reportlab.graphics import renderPDF
from svglib.svglib import svg2rlg

ROOT=Path(__file__).resolve().parent
SOURCE=ROOT.parent/'LogForge_论文资料'
TITLE='LogForge Source Code Analysis and Mathematical Proof - Academic Paper'
PDF=ROOT.parent/(TITLE+'_enus.pdf')
ASSETS=ROOT/'figures'
SHA='913e4b9417fa0f32b88629c39062b54589d9dd21'
BASE=f'https://github.com/HongHuo0728/LogForge/blob/{SHA}/'
NAVY='#163247';TEAL='#007E87';GREY='#586876'
PAGEW,PAGEH=A4;LEFT=53;RIGHT=53;WIDTH=PAGEW-LEFT-RIGHT

# Parse only the static source-location mapping; never execute or edit the Chinese builder.
tree=ast.parse((SOURCE/'build_paper.py').read_text(encoding='utf-8'))
CHAPTER_FILES=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='CHAPTER_FILES' for t in n.targets))

ROLES={
'AudioPayload':'Per-stream SHA-256 over copied audio packet content; Chapter 13',
'v130':'Queue, report and CUDA cache regressions; Chapters 11, 20, 21',
'cadence_v130':'Full-sequence rational quantization regressions; Chapters 12, 21',
'AppleLog':'Piecewise Apple Log encoding/decoding and public constants; Chapter 4',
'HLG':'Inverse HLG, reference scaling, creative adjustment, and signal counts; Chapters 3, 6',
'FloatTransformer':'Persistent CPU workers, tile pairing, and statistics merging; Chapter 10',
'CudaTransformer':'Dynamic driver interface, qualification, and transactional commit; Chapter 11',
'FloatBridge':'Three-slot read/transform/write state machine and thread budgeting; Chapter 9',
'Cadence':'All-packet period, phase, and fixed-clock validation; Chapter 12',
'MediaSafety':'Time semantics, display matrices, and fixed-width header repair; Chapters 13, 16',
'Media':'Probe parsing, admission, metadata arguments, and output validation; Chapters 2, 16',
'MetadataPolicy':'Metadata allowlists and conflict classification; Chapter 16',
'MovAnalyzer':'Bounded atom parsing and typed semantic comparison; Chapter 14',
'AppleLogIdentification':'Writing the 35-byte identification extension and checking structure; Chapter 15',
'FFmpegNumeric':'Independent integer signals and actual codec numerical qualification; Chapters 5, 7',
'FFmpeg':'Tool capabilities, discovery scheduling, and fixed downloads; Chapter 17',
'ToolTrust':'Identity, approval, and leases for the executable pair; Chapter 17',
'StorageSafety':'File-identity journals, state locks, and ownership-based recovery; Chapter 19',
'Platform':'Wide strings, handles, process supervision, pipes, and hashing; Chapter 18',
'Transcode':'Complete media task, backends, validation reports, and final publication; Chapters 8, 23',
'PixelSanity':'Nine-block gross-error checks on first, middle, and last encoded frames; Chapter 7',
'Queue':'Serial multi-file tasks and per-item failure recording; Chapter 20',
'Discovery':'Local volume and directory traversal; Chapter 17',
'DiscoveryHelper':'Supervised hidden discovery entry point and source budgets; Chapter 17',
'Cli':'Commands, numeric arguments, and automation entry points; Chapter 20',
'MainWindow':'Single background worker, events, and measured layout; Chapter 20',
'SettingsWindow':'Settings drafts, save/cancel, candidate review, and dialogs; Chapter 20',
'Ui':'Themes, DPI, native drawing, and screenshots; Chapter 20',
'Settings':'Field merging, concurrency locks, and atomic preference persistence; Chapters 19, 20',
'Localization':'Stable message keys and single-pass argument replacement; Chapter 20',
'Color':'Color-model interfaces, constants, parameters, and statistics; Chapters 3-7',
'tests':'Basic mathematical, media, and platform tests; Chapter 21',
'application':'Preferences, discovery, and version-resource tests; Chapter 21',
'hardening':'Cadence, tool trust, MOV, parallelism, and publication regressions; Chapter 21',
'reliability':'Process, discovery, and CLI abnormal-path regressions; Chapter 21',
'v120':'Storage, matrices, CUDA faults, queues, and media regressions; Chapter 21',
'identification':'Identification writing, idempotence, and structural-failure tests; Chapters 15, 21',
'integration':'End-to-end synthetic media; 1.3.0 release verification passes explicit conflict and override regressions; Chapter 21',
'compatibility_v121':'Portrait pixels, matrices, and audio-offset regression; Chapters 13, 21',
'signal_integrity':'Independent integer patches, creative processing, and range counts; Chapters 7, 21',
'sustained':'240-frame 4K120, memory, and post-encoding luma; Chapter 21',
'benchmark':'Formal throughput matrix and process observations; Chapter 22',
'cuda_artifact':'CUDA source, embedded PTX hashes, and option consistency; Chapter 11',
'fake_ffmpeg':'Marker fixture establishing non-execution of unknown tools; Chapter 17',
'process_fixture':'Descendant-held pipes, long lines, and exit-code fixtures; Chapter 18',
'resolve_identification':'Real Resolve controlled-import script; not rerun for this paper; Chapter 15',
'gui_smoke':'Interactive desktop GUI scenarios; not rerun for this paper; Chapter 20',
'layout_snapshots':'Production layout, theme, and DPI screenshot assertions; Chapter 20',
'package_audit':'Archive allowlist, CRC, executable, and SHA-256 audit; Appendix',
'compile_cuda':'NVRTC compilation with precise options and generated manifest; Chapter 11'}

def role(path):
    if path=='src/color/ColorKernel.ptx.h':return 'Generated PTX byte array: content-hash and source-manifest checks; not handwritten algorithm code; Chapter 11'
    if path=='src/color/ColorKernel.cu':return 'Device-side double color kernel and shared-memory reduction; Chapter 11'
    if path.endswith('ColorKernel.manifest.json'):return 'CUDA source, PTX, compiler identity, and precise options; Chapter 11'
    if path=='third_party/nlohmann/json.hpp':return 'Third-party JSON 3.12.0 single-header dependency for JSON I/O; not the project color algorithm'
    if 'LICENSE' in path or path.endswith('NOTICE.txt') or path=='THIRD_PARTY_NOTICES.md':return 'Project or third-party licensing and attribution; distribution boundaries'
    if path.endswith('Messages.inc'):return 'Bilingual message macro table, stable keys, and placeholders; Chapter 20'
    if path.startswith('docs/') or path in ['README.md','CHANGELOG.md','CONTRIBUTING.md']:return 'Project documentation, historical evidence, or development procedures; cross-checked by chapter topic'
    if path.startswith('resources/'):return 'Icons, manifests, version resources, and generated templates; build and interface'
    if path.startswith('.github/') or path=='CMakeLists.txt':return 'C++20/MSVC targets, test registration, build and release allowlists; Chapters 8, 21'
    if path.startswith('.'):return 'Repository formatting, line endings, and ignore rules; reproduction infrastructure'
    return ROLES.get(Path(path).stem, 'Version 1.3.0 implementation, interface, or supporting evidence')

def prepare():
    ASSETS.mkdir(exist_ok=True)
    for p in (SOURCE/'figures').iterdir():
        if p.is_file():shutil.copy2(p,ASSETS/p.name)
    (ROOT/'evidence').mkdir(exist_ok=True)
    for p in (SOURCE/'evidence').iterdir():
        if p.is_file():shutil.copy2(p,ROOT/'evidence'/p.name)
    shutil.copy2(SOURCE/'本次回归测试.xml',ROOT/'evidence/ctest_current.xml')
    shutil.copy2(SOURCE/'LogForge_913e4b9_源码快照.zip',ROOT/'LogForge_913e4b9_source_snapshot.zip')
    data=json.loads((SOURCE/'全仓库源码清单.json').read_text(encoding='utf-8'))
    rows=data['files']
    for r in rows:
        r['role']=role(r['path'])
        # Keep the full source-byte provenance; generated function text remains in the original manifest.
        r.pop('symbols',None)
    (ROOT/'source_inventory_en.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
    (ROOT/'source_inventory_en.tsv').write_text('path\tlines\tbytes\tsha256\trole\n'+'\n'.join(f"{r['path']}\t{r['lines']}\t{r['bytes']}\t{r['sha256']}\t{r['role']}" for r in rows),encoding='utf-8')
    return rows

for name,file in [('Body','times.ttf'),('BodyBold','timesbd.ttf'),('BodyItalic','timesi.ttf'),('Sans','arial.ttf'),('SansBold','arialbd.ttf')]:
    pdfmetrics.registerFont(TTFont(name,'C:/Windows/Fonts/'+file))
pdfmetrics.registerFontFamily('Body',normal='Body',bold='BodyBold',italic='BodyItalic',boldItalic='BodyBold')
ST={
'body':ParagraphStyle('body',fontName='Body',fontSize=10.6,leading=14.2,spaceAfter=6,firstLineIndent=14,alignment=TA_JUSTIFY,textColor=colors.HexColor('#202D35'),allowWidows=0,allowOrphans=0,splitLongWords=True),
'h1':ParagraphStyle('h1',fontName='SansBold',fontSize=18,leading=24,spaceAfter=17,textColor=colors.HexColor(NAVY),keepWithNext=True),
'h2':ParagraphStyle('h2',fontName='Sans',fontSize=11.6,leading=16,spaceBefore=12,spaceAfter=6,textColor=colors.HexColor(TEAL),keepWithNext=True),
'cap':ParagraphStyle('cap',fontName='Body',fontSize=9,leading=12,spaceBefore=6,spaceAfter=11,textColor=colors.HexColor(GREY)),
'small':ParagraphStyle('small',fontName='Body',fontSize=8.7,leading=12,spaceAfter=6,textColor=colors.HexColor(GREY)),
'cell':ParagraphStyle('cell',fontName='Body',fontSize=8.5,leading=11.1,splitLongWords=True),
'th':ParagraphStyle('th',fontName='SansBold',fontSize=8.2,leading=11,textColor=colors.white),
'ref':ParagraphStyle('ref',fontName='Body',fontSize=9,leading=12.5,spaceAfter=10,splitLongWords=True)}
def p(t,style='body'):return Paragraph(escape(t),ST[style])

class Vector(Flowable):
    def __init__(self,path,maxheight=290):
        super().__init__();self.drawing=svg2rlg(str(path));d=self.drawing
        self.scale=min(WIDTH/d.width,maxheight/d.height,1)
        self.width=d.width*self.scale;self.height=d.height*self.scale;self.hAlign='CENTER'
    def draw(self):
        self.canv.saveState();self.canv.scale(self.scale,self.scale);renderPDF.draw(self.drawing,self.canv,0,0);self.canv.restoreState()

class Eq(Flowable):
    def __init__(self,num):
        super().__init__();self.num=num
        self.drawing=svg2rlg(str(ASSETS/f'equation_{num:03}.svg'))
        self.scale=min(1,(WIDTH-50)/self.drawing.width)
        self.width=WIDTH;self.height=max(27,self.drawing.height*self.scale+15)
    def draw(self):
        d=self.drawing;s=self.scale
        self.canv.saveState();self.canv.translate((WIDTH-d.width*s)/2,8);self.canv.scale(s,s);renderPDF.draw(d,self.canv,0,0);self.canv.restoreState()
        self.canv.setFont('Body',10);self.canv.setFillColor(colors.HexColor(GREY));self.canv.drawRightString(WIDTH,self.height/2-3,f'({self.num})')

def table(rows,widths=None):
    n=len(rows[0]);widths=widths or [WIDTH/n]*n
    data=[[p(str(x),'th' if i==0 else 'cell') for x in row] for i,row in enumerate(rows)]
    t=Table(data,colWidths=widths,repeatRows=1,hAlign='LEFT')
    t.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,0),colors.HexColor(NAVY)),('VALIGN',(0,0),(-1,-1),'TOP'),('ROWBACKGROUNDS',(0,1),(-1,-1),[colors.white,colors.HexColor('#F1F5F6')]),('LINEBELOW',(0,0),(-1,0),.7,colors.HexColor(TEAL)),('LINEBELOW',(0,1),(-1,-1),.3,colors.HexColor('#D6E0E5')),('LEFTPADDING',(0,0),(-1,-1),7),('RIGHTPADDING',(0,0),(-1,-1),7),('TOPPADDING',(0,0),(-1,-1),6),('BOTTOMPADDING',(0,0),(-1,-1),6)]))
    return t

class Paper(BaseDocTemplate):
    def __init__(self):
        super().__init__(str(PDF),pagesize=A4,leftMargin=LEFT,rightMargin=RIGHT,topMargin=57,bottomMargin=51,title=TITLE,author='Source-code research and technical analysis',subject='English edition; fixed revision 913e4b9; version 1.3.0',pageCompression=1)
        f=Frame(LEFT,51,WIDTH,PAGEH-108,id='normal',leftPadding=0,rightPadding=0,topPadding=0,bottomPadding=0)
        self.addPageTemplates([PageTemplate(id='cover',frames=[f],onPage=lambda c,d:None),PageTemplate(id='main',frames=[f],onPage=self.header)])
    def beforeDocument(self):self.hcount=0
    def header(self,c,doc):
        c.saveState();c.setStrokeColor(colors.HexColor(TEAL));c.setLineWidth(.55);c.line(LEFT,PAGEH-35,PAGEW-RIGHT,PAGEH-35)
        c.setFont('SansBold',7.6);c.setFillColor(colors.HexColor(NAVY));c.drawString(LEFT,PAGEH-27,'LOGFORGE  |  SOURCE & MATHEMATICS')
        c.setFont('Body',8);c.drawRightString(PAGEW-RIGHT,PAGEH-27,'1.3.0  |  26929A  |  913e4b9')
        c.setStrokeColor(colors.HexColor('#CBD6DC'));c.line(LEFT,36,PAGEW-RIGHT,36)
        c.setFont('Body',8);c.setFillColor(colors.HexColor(GREY));c.drawString(LEFT,24,'English edition  |  Fixed-revision study  |  30 September 2026')
        c.drawRightString(PAGEW-RIGHT,24,str(doc.page));c.restoreState()
    def afterFlowable(self,flow):
        if isinstance(flow,Paragraph) and flow.style.name=='h1':
            title=flow.getPlainText();key=f'section_{self.hcount}';self.hcount+=1
            self.canv.bookmarkPage(key);self.canv.addOutlineEntry(title,key,0,False)
            if title not in ['Contents','Edition and Evidence Notes']:self.notify('TOCEntry',(0,title,self.page,key))

def references():
    return [
    ('Project source and README at the fixed revision',f'https://github.com/HongHuo0728/LogForge/tree/{SHA}'),
    ('LogForge COLOR_PIPELINE: numerical contract',BASE+'docs/COLOR_PIPELINE.md'),
    ('LogForge CREATIVE_ADJUSTMENTS: project-defined adjustments',BASE+'docs/CREATIVE_ADJUSTMENTS.md'),
    ('LogForge ARCHITECTURE: architecture and historical policies',BASE+'docs/ARCHITECTURE.md'),
    ('LogForge RELEASE_1.3.0: current admission and raster-orientation policy',BASE+'docs/RELEASE_1.3.0.md'),
    ('LogForge VALIDATION: versioned historical validation records',BASE+'docs/VALIDATION.md'),
    ('LogForge BENCHMARK_1.2.0: historical performance matrix',BASE+'docs/BENCHMARK_1.2.0.md'),
    ('LogForge APPLE_LOG_IDENTIFICATION: native references and controlled Resolve experiments',BASE+'docs/APPLE_LOG_IDENTIFICATION.md'),
    ('LogForge METADATA and FFMPEG_PROVIDER: container and tool policies',BASE+'docs/METADATA.md'),
    ('LogForge TECHNICAL_REFERENCES: index of external primary references',BASE+'docs/TECHNICAL_REFERENCES.md'),
    ('Apple-supplied ACES IDT, fixed commit 528c78fe: Apple Log decoding constants','https://github.com/ampas/aces-dev/blob/528c78fe2c0f4e7eb322581e98aba05de79466cb/transforms/ctl/idt/vendorSupplied/apple/IDT.Apple.AppleLog_BT2020.ctl'),
    ('ITU-R BT.2100-3 (2025): HLG transfer function; official version index checked for this study','https://www.itu.int/rec/R-REC-BT.2100'),
    ('ITU-R BT.2408-6: HDR production reference; scaling is cross-explained against the fixed repository contract','https://www.itu.int/dms_pub/itu-r/opb/rep/R-REP-BT.2408-6-2023-PDF-E.pdf'),
    ('ITU-R BT.2020: UHD television primaries and matrix','https://www.itu.int/rec/R-REC-BT.2020'),
    ('FFmpeg official filter manual: zscale; concrete behavior is constrained by qualification tests','https://ffmpeg.org/ffmpeg-filters.html#zscale'),
    ('FFmpeg n8.1.2 MOV muxer source: the fixed version referenced by the repository','https://github.com/FFmpeg/FFmpeg/blob/n8.1.2/libavformat/movenc.c'),
    ('Apple QuickTime File Format: original matrix and container documentation','https://developer.apple.com/documentation/quicktime-file-format/matrices'),
    ('NVIDIA NVRTC 12.8.1: interface reference for PTX generation','https://docs.nvidia.com/cuda/archive/12.8.1/nvrtc/index.html'),
    ('Microsoft Windows: Job Objects supervision interface','https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects'),
    ('CMake CPack: archive and checksum-generation reference','https://cmake.org/cmake/help/latest/module/CPack.html')]

def texesc(t):
    return ''.join({'\\':r'\textbackslash{}','&':r'\&','%':r'\%','$':r'\$','#':r'\#','_':r'\_','{':r'\{','}':r'\}','~':r'\textasciitilde{}','^':r'\textasciicircum{}'}.get(c,c) for c in t)

def tex_table(rows,caption):
    spec=''.join('p{'+str(round(.86/len(rows[0]),3))+'\\linewidth}' for _ in rows[0])
    return '\n\\begin{longtable}{'+spec+'}\n\\caption{'+texesc(caption)+'}\\\\\n\\toprule\n'+'\\\\\n'.join(' & '.join(texesc(str(v)) for v in row) for row in rows)+'\\\\\n\\bottomrule\n\\end{longtable}\n'

def build(rows):
    source=(ROOT/'manuscript_en.md').read_text(encoding='utf-8')
    translated=json.loads((ROOT/'translation_checks.json').read_text(encoding='utf-8'))
    if translated['problems']:raise RuntimeError('Translation integrity checks have unresolved differences')
    original=(SOURCE/'论文正文.md').read_text(encoding='utf-8')
    assert re.findall(r'^\$\$(.*)\$\$$',source,re.M)==re.findall(r'^\$\$(.*)\$\$$',original,re.M)
    story=[];tex=[];eqnum=0;fignum=0;tabnum=0;chapter=0;figs=[]
    cover=ParagraphStyle('coverTitle',fontName='SansBold',fontSize=29,leading=36,textColor=colors.HexColor(NAVY))
    story.extend([Spacer(1,63),p('SOURCE CODE MONOGRAPH  /  ENGLISH EDITION','small'),Spacer(1,19),Paragraph('LogForge',ParagraphStyle('brand',fontName='BodyBold',fontSize=47,leading=54,textColor=colors.HexColor(TEAL))),Spacer(1,19),Paragraph('Source Code Analysis<br/>and Mathematical Proof<br/>- Academic Paper',cover),Spacer(1,22),p('HLG to Apple Log: from color mathematics to reliable media delivery','h2'),Spacer(1,21),table([
    ['Research object','Fixed revision and evidence'],['Repository','HongHuo0728 / LogForge'],['Version','1.3.0 (26929A)'],['Commit',SHA],['Study date','30 September 2026'],['Edition','Complete English translation of the Chinese research paper'],['Structure','24 chapters; 66 equations; 18 vector figures; 5 tables'],['Validation','Release build passed; 29 CTest entries: 29 passed, 0 failed']], [112,WIDTH-112]),Spacer(1,23),p('An inspectable technical study of public source code. No author affiliation, publication status, camera certification, or universal editor compatibility is asserted.','small'),NextPageTemplate('main'),PageBreak()])
    story.append(p('Contents','h1'))
    toc=TableOfContents(tableStyle=TableStyle([('LEFTPADDING',(0,0),(-1,-1),0),('RIGHTPADDING',(0,0),(-1,-1),0),('TOPPADDING',(0,0),(-1,-1),0),('BOTTOMPADDING',(0,0),(-1,-1),0)]))
    toc.levelStyles=[ParagraphStyle('toc',fontName='Body',fontSize=9.7,leading=20,leftIndent=0,firstLineIndent=0,spaceBefore=0,textColor=colors.HexColor(NAVY))]
    story.extend([toc,PageBreak(),p('Edition and Evidence Notes','h1'),p('This English edition and its Chinese counterpart are revised together for LogForge 1.3.0 at the recorded source revision. They replace the previous 1.2.1 editions. Existing release verification, historical measurements, and mathematical derivations retain distinct provenance.'),p('The title uses mathematical proof to refer to the conditional derivations and structural arguments developed in the text. It does not assert machine-checked, whole-program verification. The reported tests belong to the original study; preparing this translation is not a new experimental run.'),p('Mathematical curves are calculated from public constants and current source formulas. Architecture and state diagrams abstract implementation relationships. Figures labeled historical use repository version 1.2.0 measurements; figures describing the historical 1.2.1 reproduction retain the earlier study records. No illustrative curve is presented as an unreported experiment.'),p('Equations, figures, and tables retain separate continuous numbering. Principal source files appear at the end of each chapter, linked to the fixed commit. Appendix B and the companion inventory cover every source and project file (excluding paper artifacts).'),p('Reading route: Chapters 1-7 establish the signal and numerical model; Chapters 8-12 explain execution and clocks; Chapters 13-19 examine media semantics and reliability; Chapters 20-24 connect interfaces, reproduction results, performance, the implementation walkthrough, and conclusions.'),PageBreak()])
    def citations(n):
        if n not in CHAPTER_FILES:return
        links='Principal source files: '+'; '.join(f'<link href="{BASE+f}" color="{TEAL}">{escape(f)}</link>' for f in CHAPTER_FILES[n])+'.'
        last=story.pop()
        group=[last,Spacer(1,7),Paragraph(links,ST['small'])]
        if story and isinstance(story[-1],Paragraph) and story[-1].style.name=='h2':group.insert(0,story.pop())
        story.append(KeepTogether(group))
        tex.append('\n\\par\\small '+texesc('Principal source files: '+'; '.join(CHAPTER_FILES[n])+'.')+'\\normalsize\n')
    lines=source.splitlines();i=0
    while i<len(lines):
        l=lines[i].strip();i+=1
        if not l:continue
        if l.startswith('# '):
            citations(chapter);title=l[2:]
            if title!='Abstract':story.append(PageBreak())
            m=re.match(r'Chapter (\d+)\.',title);chapter=int(m.group(1)) if m else 0
            story.append(p(title,'h1'));tex.append('\n\\chapter*{'+texesc(title)+'}\\addcontentsline{toc}{chapter}{'+texesc(title)+'}\n')
        elif l.startswith('## '):story.append(p(l[3:],'h2'));tex.append('\n\\section*{'+texesc(l[3:])+'}\n')
        elif l.startswith('$$'):
            eqnum+=1;story.append(Eq(eqnum));tex.append('\n\\begin{equation}\n'+l[2:-2]+'\n\\end{equation}\n')
        elif l.startswith('@figure:'):
            key,caption=l[8:].split('|',1);fignum+=1;figs.append({'number':fignum,'id':key,'caption':caption})
            story.append(KeepTogether([Spacer(1,7),Vector(ASSETS/f'{key}.svg'),p(f'Figure {fignum}. {caption}','cap')]))
            tex.append('\n\\begin{figure}[htbp]\\centering\\includegraphics[width=\\linewidth]{figures/'+key+'.pdf}\\caption{'+texesc(caption)+'}\\end{figure}\n')
        elif l.startswith('@table:'):
            caption=l[7:];data=[]
            while i<len(lines) and lines[i].strip()!='@end':data.append(lines[i].strip().split('|'));i+=1
            i+=1;tabnum+=1;story.append(KeepTogether([p(f'Table {tabnum}. {caption}','cap'),table(data),Spacer(1,8)]));tex.append(tex_table(data,caption))
        elif l=='@results':
            xr=ET.parse(ROOT/'evidence/ctest_current.xml').getroot();data=[['Test name','Outcome','Time (s)']]
            for x in xr.iter('testcase'):data.append([x.attrib['name'],'Failed' if x.find('failure') is not None else 'Passed',f"{float(x.attrib.get('time',0)):.3f}"])
            caption='Complete CTest results from the existing 1.3.0 release record';tabnum+=1
            story.append(KeepTogether([p(f'Table {tabnum}. {caption}','cap'),table(data,[WIDTH*.63,WIDTH*.17,WIDTH*.20]),Spacer(1,8)]));tex.append(tex_table(data,caption))
        elif l=='@inventory':
            caption=f'All {len(rows)} version-controlled files and their roles';tabnum+=1
            data=[['File path and extent','Implementation responsibility / research use']]
            for r in rows:data.append([r['path']+'\n'+(f"{r['lines']} lines" if r['lines'] is not None else f"Binary: {r['bytes']} bytes"),r['role']])
            story.extend([p(f'Table {tabnum}. {caption}','cap'),table(data,[WIDTH*.44,WIDTH*.56])]);tex.append(tex_table(data,caption))
        elif l=='@references':
            for n,(name,url) in enumerate(references(),1):
                if n==11:story.extend([PageBreak(),p('References and Source Locations (continued)','h2')])
                story.append(Paragraph(f'[{n}] {escape(name)}.<br/><link href="{escape(url)}" color="{TEAL}">{escape(url)}</link>',ST['ref']))
                tex.append('\\par ['+str(n)+'] '+texesc(name)+'. \\url{'+url+'}\\par\n')
        else:story.append(p(l));tex.append(texesc(l)+'\n\n')
    Paper().multiBuild(story)
    preamble=r'''\documentclass[11pt,a4paper,openany]{book}
\usepackage[margin=24mm]{geometry}
\usepackage[T1]{fontenc}
\usepackage{amsmath,amssymb,booktabs,longtable,graphicx,xcolor,hyperref,fancyhdr}
\definecolor{navy}{HTML}{163247}
\hypersetup{colorlinks=true,linkcolor=navy,urlcolor=navy}
\setlength{\parskip}{5pt}
\pagestyle{fancy}\fancyhf{}\fancyhead[L]{LogForge: Source Code Analysis and Mathematical Proof}\fancyfoot[C]{\thepage}
\title{LogForge Source Code Analysis and Mathematical Proof - Academic Paper}
\author{Public-source technical research paper}\date{30 September 2026}
\begin{document}\maketitle\tableofcontents
'''
    (ROOT/'LogForge_Academic_Paper_English.tex').write_text(preamble+''.join(tex)+'\n\\end{document}\n',encoding='utf-8')
    (ROOT/'figure_index_en.json').write_text(json.dumps(figs,indent=2),encoding='utf-8')
    doc=fitz.open(PDF)
    metrics={'title':TITLE,'commit':SHA,'version':'1.3.0 (26929A)','pages':len(doc),'chapters':24,'subsections':translated['subsections'],'english_words':translated['english_words'],'equations':eqnum,'figures':fignum,'tables':tabnum,'tracked_files':len(rows),'pdf_sha256':hashlib.sha256(PDF.read_bytes()).hexdigest(),'ctest':{'tests':29,'passed':29,'failed':0,'provenance':'docs/verification/1.3.0.json; existing release record','provenance':'existing 1.3.0 release record; no new media run for revision'},'pdf_export':'ReportLab; embedded fonts; original vector equations and figures; translated captions and tables','latex_status':'Editable project source; built-in compiler unavailable during original study; compilation not asserted'}
    (ROOT/'production_metrics.json').write_text(json.dumps(metrics,indent=2),encoding='utf-8')
    print(json.dumps(metrics,indent=2))

if __name__=='__main__':build(prepare())
