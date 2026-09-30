from pathlib import Path
import zipfile
import re, json, hashlib, subprocess, shutil, io, math, xml.etree.ElementTree as ET
from xml.sax.saxutils import escape
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, Rectangle, FancyArrowPatch
from matplotlib import mathtext
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.enums import TA_JUSTIFY, TA_CENTER, TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.platypus import (BaseDocTemplate, PageTemplate, Frame, Paragraph, Spacer, PageBreak,
    Table, TableStyle, KeepTogether, Flowable, NextPageTemplate)
from reportlab.platypus.tableofcontents import TableOfContents
from reportlab.graphics import renderPDF
from svglib.svglib import svg2rlg
import fitz

ROOT=Path(__file__).resolve().parent
REPO=Path('D:/LogForge')
ASSETS=ROOT/'figures'; ASSETS.mkdir(exist_ok=True)
EVID=ROOT/'evidence'; EVID.mkdir(exist_ok=True)
PDF=ROOT.parent/'LogForge_源码解析与数学论证_学术论文_zhcn.pdf'
SHA='913e4b9417fa0f32b88629c39062b54589d9dd21'
BASE=f'https://github.com/HongHuo0728/LogForge/blob/{SHA}/'
NAVY='#163247'; TEAL='#007E87'; BLUE='#3576B5'; ORANGE='#BD672A'; GREY='#586876'
plt.rcParams.update({'font.family':'DejaVu Sans','font.size':9,'axes.titlesize':11,
 'axes.labelsize':9,'axes.spines.top':False,'axes.spines.right':False,'axes.edgecolor':'#A5B1B8',
 'axes.labelcolor':NAVY,'xtick.color':GREY,'ytick.color':GREY,'text.color':NAVY,
 'grid.color':'#DAE2E6','grid.linewidth':.55,'svg.fonttype':'path','pdf.fonttype':42,
 'mathtext.fontset':'stix','figure.facecolor':'white','savefig.facecolor':'white'})

def invhlg(x):
    x=np.asarray(x,dtype=float); a=.17883277;b=1-4*a;c=.5-a*np.log(4*a)
    return np.where(x<0,-x*x/3,np.where(x<=.5,x*x/3,(np.exp((x-c)/a)+b)/12))
S=1/float(invhlg(.75))
def apple(r):
    r=np.asarray(r,dtype=float)
    return np.where(r<-.05641088,0,np.where(r<.01,47.28711236*(r+.05641088)**2,
           .08550479*np.log2(np.maximum(r+.00964052,1e-99))+.69336945))
def grade(x,s=3,h=1):
    t=np.minimum(abs(x)/6,1); w=t*t*(3-2*t)
    return x+np.where(x<0,s*w,-h*w)
def save(fig,name):
    fig.savefig(ASSETS/f'{name}.svg',bbox_inches='tight',pad_inches=.10)
    fig.savefig(ASSETS/f'{name}.pdf',bbox_inches='tight',pad_inches=.10)
    fig.savefig(ASSETS/f'{name}.png',dpi=160,bbox_inches='tight',pad_inches=.10)
    plt.close(fig)
def setup(n=1,ratio=3.0):
    fig,ax=plt.subplots(1,n,figsize=(7.0,ratio),layout='constrained')
    for a in np.atleast_1d(ax):a.grid(alpha=.75);a.set_axisbelow(True)
    return fig,ax
def box(ax,x,y,w,h,t,c=TEAL,fs=9):
    ax.add_patch(FancyBboxPatch((x,y),w,h,boxstyle='round,pad=0.025,rounding_size=0.09',
         fc=c,ec='white',lw=.8))
    ax.text(x+w/2,y+h/2,t,ha='center',va='center',color='white',fontsize=fs,linespacing=1.5)
def arrow(ax,p,q,label=None):
    ax.add_patch(FancyArrowPatch(p,q,arrowstyle='-|>',mutation_scale=11,lw=1.1,color=GREY))
    if label:ax.text((p[0]+q[0])/2,(p[1]+q[1])/2+.13,label,fontsize=8,ha='center')
def diagram(name,title,rows):
    fig,ax=plt.subplots(figsize=(7,1.1+1.25*len(rows)))
    ax.set_xlim(-.1,10.1);ax.set_ylim(-.25,len(rows)*1.6+.5);ax.axis('off')
    ax.set_title(title,loc='left',pad=12,fontweight='bold')
    for ri,row in enumerate(rows):
        y=(len(rows)-1-ri)*1.6+.15; n=len(row); w=(10-(n-1)*.4)/n
        for j,t in enumerate(row):
            pos=j if ri%2==0 else n-1-j
            x=pos*(w+.4)
            color=BLUE if ri==0 else TEAL
            if name=='pipeline':color=[[GREY,BLUE,TEAL],[TEAL,BLUE,TEAL]][ri][j]
            box(ax,x,y,w,.85,t,color,8.5)
            if j<n-1:
                if ri%2==0:arrow(ax,(x+w+.025,y+.42),(x+w+.375,y+.42))
                else:arrow(ax,(x-.025,y+.42),(x-.375,y+.42))
        if ri<len(rows)-1:
            edge=10-w/2 if ri%2==0 else w/2
            arrow(ax,(edge,y-.02),(edge,y-.72))
    save(fig,name)

def figures():
    diagram('pipeline','Signal domains and production boundaries',[
      ['ProRes HLG\n10-bit YCbCr','zscale decode\nHLG GBR float32','LogForge\ndouble color math'],
      ['Apple Log RGB\nfloat32 transport','zscale + ProRes HQ\n10-bit 4:2:2','MOV validation\nlogs + nclc 9/2/9']])
    fig,ax=setup(2,3.1); h=np.linspace(0,1,1201)
    ax[0].plot(h,invhlg(h),c=BLUE,lw=1.8);ax[0].axvline(.5,c=GREY,ls=':',lw=1)
    ax[0].set(xlabel='HLG encoded component H',ylabel='Relative scene signal E',title='(a) Inverse HLG OETF')
    ax[1].plot(h,S*invhlg(h),c=TEAL,lw=1.8)
    for u,r,l in [(.3782588831,.18,'18% gray'),(.75,1,'Reference white')]:
        ax[1].scatter([u],[r],c=ORANGE,s=22);ax[1].annotate(l,(u,r),xytext=(5,8),textcoords='offset points',fontsize=8)
    ax[1].set(xlabel='HLG encoded component H',ylabel='Reflectance coordinate R',title='(b) Reference scaling, EV = 0');save(fig,'hlg')
    fig,ax=setup(2,3.1);r=np.linspace(-.06,.04,1000)
    ax[0].plot(r,apple(r),c=BLUE,lw=1.8);ax[0].axvline(.01,c=GREY,ls=':');ax[0].axvline(-.05641088,c=GREY,ls=':')
    ax[0].set(xlabel='Scene-linear R',ylabel='Apple Log P',title='(a) Floor and quadratic toe')
    r=np.geomspace(.01,16,1000);ax[1].semilogx(r,apple(r),c=TEAL,lw=1.8)
    for x,l in [(.18,'18%'),(12,'P = 1')]:ax[1].scatter([x],apple(x),c=ORANGE,s=20);ax[1].annotate(l,(x,float(apple(x))),xytext=(5,7),textcoords='offset points',fontsize=8)
    ax[1].set(xlabel='Scene-linear R (log scale)',ylabel='Apple Log P',title='(b) Logarithmic branch');save(fig,'apple')
    fig,ax=setup(2,3.1);x=np.linspace(-9,9,1801)
    ax[0].plot(x,x,c=GREY,ls='--',label='Disabled');ax[0].plot(x,grade(x),c=TEAL,label='s=3, h=1');ax[0].plot(x,grade(x,3,3),c=ORANGE,label='s=3, h=3')
    ax[0].legend(fontsize=8);ax[0].set(xlabel='Input stops relative to 18% gray',ylabel='Output stops',title='(a) Global luminance mapping')
    ax[1].plot(x,np.gradient(grade(x,3,3),x),c=TEAL,lw=1.7);ax[1].axhline(.25,c=ORANGE,ls='--',label='Proven lower bound');ax[1].legend(fontsize=8)
    ax[1].set(xlabel='Input stops relative to 18% gray',ylabel='Local log-domain slope',ylim=(0,1.12),title='(b) Monotonicity, s=h=3');save(fig,'creative')
    fig,ax=setup(2,3);q=np.arange(64,941);out=64+876*apple(S*invhlg((q-64)/876))
    ax[0].plot(q,out,c=BLUE);ax[0].set(xlabel='Input HLG luma code',ylabel='Ideal Apple Log luma code',title='(a) Neutral code mapping')
    ax[1].step(q[:61],np.rint(out[:61]),where='mid',c=TEAL);ax[1].plot(q[:61],out[:61],c=ORANGE,lw=1,ls='--',label='Before rounding');ax[1].legend(fontsize=8)
    ax[1].set(xlabel='Input HLG luma code',ylabel='Rounded output code',title='(b) Many-to-one shadow mapping');save(fig,'quantization')
    fig,ax=plt.subplots(figsize=(7,2.6));ax.set_xlim(-.5,8);ax.set_ylim(-.6,2.8);ax.set_yticks([0,1,2],['Center chroma','Left chroma','Luma']);ax.set_xlabel('Horizontal luma pixel coordinate')
    for y,xs,c in [(2,np.arange(8),NAVY),(1,np.arange(0,8,2),BLUE),(0,np.arange(.5,8,2),TEAL)]:
        ax.hlines(y,-.25,7.6,color='#CFD8DE',lw=1);ax.scatter(xs,np.full(len(xs),y),c=c,s=45,zorder=3)
    ax.annotate('',xy=(.5,.4),xytext=(0,.4),arrowprops={'arrowstyle':'<->','color':ORANGE});ax.text(.9,.35,'Half-pixel phase = 0.5',c=ORANGE,fontsize=9)
    ax.spines[['left','top','right']].set_visible(False);save(fig,'chroma')
    fig,ax=plt.subplots(figsize=(7,4));ax.axis('off');ax.set_xlim(-.1,10.1);ax.set_ylim(-.1,5.2)
    ax.set_title('Application layers and orchestration relationships',loc='left',fontweight='bold',pad=12)
    box(ax,1,4.1,3,.75,'Win32 GUI\nMainWindow / Settings',BLUE,8.5)
    box(ax,6,4.1,3,.75,'CLI automation\nCli.cpp',BLUE,8.5)
    box(ax,3.5,2.6,3,.75,'TranscodeJob\nLifecycle owner',TEAL,8.5)
    box(ax,0,2.6,2.8,.75,'Trust + Probe\nTools / Media',GREY,8.5)
    box(ax,7.2,2.6,2.8,.75,'Validation + Publish\nMedia / MOV / Storage',GREY,8)
    box(ax,3.5,1.2,3,.75,'FloatBridge\nOrdered buffers',TEAL,8.5)
    box(ax,0,.1,2.8,.75,'CPU / CUDA\nColor transform',TEAL,8.5)
    box(ax,7.2,.1,2.8,.75,'FFmpeg processes\nDecode / Encode',BLUE,8.5)
    for p0,p1 in [((2.5,4.05),(4.3,3.4)),((7.5,4.05),(5.7,3.4)),((3.45,2.98),(2.85,2.98)),((6.55,2.98),(7.15,2.98)),((5,2.55),(5,2)),((3.45,1.5),(1.5,.9)),((6.55,1.5),(8.5,.9))]:arrow(ax,p0,p1)
    save(fig,'architecture')
    fig,ax=plt.subplots(figsize=(7,2.7)); ax.set_xlim(0,7.8);ax.set_ylim(-.7,2.8)
    for k in range(5):
        for y,off,c in [(2,0,BLUE),(1,1,TEAL),(0,2,ORANGE)]:
            ax.add_patch(Rectangle((k+off,y-.28),.92,.56,fc=c,ec='white'));ax.text(k+off+.46,y,f'{k+1}',color='white',ha='center',va='center')
    ax.set_yticks([0,1,2],['Write / encode','Color transform','Read / decode']);ax.set_xlabel('Illustrative scheduling slots (not measured seconds)');ax.set_xticks(range(8));ax.spines[['top','right','left']].set_visible(False);save(fig,'bridge')
    diagram('cuda','CUDA transaction for one host chunk',[
      ['Original host block\nUnchanged','Pinned staging\nUpload','GPU kernel\nDouble expressions'],
      ['Download + statistics','Synchronize + qualify\nAny failure: no commit','Commit pixels + counts\nAuto may retry on CPU']])
    fig,ax=setup(2,3);i=np.arange(310);pts=np.floor(i*2061/103)
    inferred=pts-i*2061/103;nominal=pts-i*20.02
    ax[0].plot(i,nominal,c=ORANGE,label='Nominal 60000/1001');ax[0].plot(i,inferred,c=TEAL,label='Inferred 41200/687');ax[0].legend(fontsize=8)
    ax[0].set(xlabel='Boundary index (final end included)',ylabel='Residual (ticks)',title='(a) Clock disagreement')
    ax[1].plot(i,inferred,c=TEAL);ax[1].axhline(0,c=GREY,ls='--');ax[1].axhline(-1,c=GREY,ls='--')
    ax[1].set(xlabel='Boundary index',ylabel='Residual (ticks)',title='(b) Shared one-tick quantization cell');save(fig,'cadence')
    fig,axes=plt.subplots(1,2,figsize=(7,3));arr=np.array([[.2,.5],[.75,.95]])
    for a,v,t in [(axes[0],arr,'Source raster + rotation metadata'),(axes[1],np.rot90(arr),'Upright pixels + identity matrix')]:
        a.imshow(v,cmap='Blues',vmin=0,vmax=1,interpolation='nearest',aspect=1.35 if a==axes[0] else .74)
        for (y,x),val in np.ndenumerate(v):a.text(x,y,f'{val:.2f}',ha='center',va='center',color='white' if val>.5 else NAVY,fontsize=12)
        a.set_xticks([]);a.set_yticks([]);a.set_title(t,fontsize=9)
    fig.tight_layout();save(fig,'rotation')
    fig,ax=plt.subplots(figsize=(7,3.7));ax.axis('off');ax.set_xlim(0,10);ax.set_ylim(0,5)
    nodes=[(.1,3.9,1.25,'moov'),(1.85,3.9,1.2,'trak'),(3.5,3.9,1.2,'mdia'),(5.15,3.9,1.2,'minf'),(6.8,3.9,1.2,'stbl'),(8.45,3.9,1.2,'stsd')]
    for x,y,w,t in nodes:box(ax,x,y,w,.6,t,BLUE)
    for a,b in zip(nodes,nodes[1:]):arrow(ax,(a[0]+a[2],4.2),(b[0],4.2))
    box(ax,7,2.3,2.65,.7,'apch: ProRes HQ',TEAL);arrow(ax,(9,3.85),(8.3,3.05))
    box(ax,4.6,.6,2.3,.85,'colr\nnclc 9 / 2 / 9',TEAL);box(ax,7.35,.6,2.3,.85,'logs\nApple Log identifier',ORANGE)
    arrow(ax,(7.8,2.25),(5.8,1.5));arrow(ax,(8.8,2.25),(8.5,1.5))
    box(ax,.1,2.25,2.4,.7,'meta / keys / ilst',GREY);arrow(ax,(.7,3.85),(1.3,3.0));ax.text(.15,1.4,'Typed movie metadata\nSeparate from sample-entry logs',fontsize=9,linespacing=1.6);save(fig,'movtree')
    fig,ax=plt.subplots(figsize=(7,2.6));ax.axis('off');ax.set_xlim(0,10);ax.set_ylim(-.1,3)
    for y in [1.6,.25]:
        box(ax,0,y,1,.65,'ftyp',GREY);box(ax,1.15,y,4.5,.65,'mdat: unchanged payload',BLUE);box(ax,5.8,y,1.5,.65,'moov head',TEAL)
    box(ax,7.45,1.6,2.4,.65,'moov tail',TEAL);box(ax,7.45,.25,.6,.65,'+35',ORANGE,8);box(ax,8.15,.25,1.7,.65,'shifted tail',TEAL)
    arrow(ax,(9.5,1.5),(8.6,1),'Copy backward');ax.text(0,2.55,'Before insertion',fontsize=10,fontweight='bold');ax.text(0,1.1,'After insertion',fontsize=10,fontweight='bold');save(fig,'insertion')
    diagram('trust','Execution admission is a sequence of distinct gates',[
      ['Discover paths\nNo execution','Review pair\nCanonical paths + hashes','Acquire lease\nLock and rehash'],
      ['Capabilities\nDecoder / Filters / Formats','Numerical qualification\nMatrix / Range / Phase','Production task\nOutput validation']])
    fig,ax=plt.subplots(figsize=(7,3.6));ax.axis('off');ax.set_xlim(-.1,10.1);ax.set_ylim(-.1,4)
    ax.set_title('Validation precedes final-file publication',loc='left',fontweight='bold',pad=12)
    for x,t in [(0,'Reserve unique partial'),(3.45,'Encode + metadata'),(6.9,'Validate + pending report')]:box(ax,x,2.7,3.1,.75,t,BLUE,8)
    arrow(ax,(3.12,3.07),(3.42,3.07));arrow(ax,(6.57,3.07),(6.87,3.07))
    box(ax,6.9,.65,3.1,.8,'No-overwrite move\nSame directory',TEAL,8.5)
    box(ax,3.45,.65,3.1,.8,'Published output\nThen update report',TEAL,8.5)
    box(ax,0,.65,3.1,.8,'On failure\nClean owned temporary files',ORANGE,8)
    arrow(ax,(8.45,2.65),(8.45,1.5),'Pass');arrow(ax,(6.85,1.05),(6.6,1.05))
    ax.plot([5,5,1.55,1.55],[2.65,2.02,2.02,1.48],c=ORANGE,ls='--',lw=1)
    ax.annotate('',xy=(1.55,1.46),xytext=(1.55,1.72),arrowprops={'arrowstyle':'-|>','color':ORANGE})
    ax.text(3.2,2.12,'Failure / cancellation',fontsize=8,c=ORANGE)
    save(fig,'publication')
    measured=json.loads((EVID/'sustained_current.json').read_text(encoding='utf-8'));mm=np.array(measured['memory_samples'])
    fig,ax=setup(1,3);ax.plot(mm[:,0],mm[:,1]/2**20,c=TEAL,lw=1.6);ax.fill_between(mm[:,0],mm[:,1]/2**20,alpha=.08,color=TEAL)
    ax.set(xlabel='Elapsed monitoring time (s)',ylabel='Application private bytes (MiB)',ylim=(0,17));ax.text(.97,.1,'Historical 1.2.1 | CPU Standard\n240 frames, 3840 x 2160',transform=ax.transAxes,ha='right',fontsize=9);save(fig,'memory')
    # Values transcribed from the repository table; immutable source retained.
    xx=np.arange(5);labels=['1080p24','4K24','4K30','4K60','4K120']
    fig,ax=setup(1,3.2)
    for y,l,c,ls in [([19.37,4.37,4.72,5.04,5.13],'CPU Standard',BLUE,'-'),([12.41,3.16,3.05,3.13,3.18],'CPU Creative',BLUE,'--'),([31.85,7.04,8.14,8.80,9.63],'CUDA Standard',TEAL,'-'),([21.66,5.22,5.12,5.67,7.07],'CUDA Creative',TEAL,'--')]:
        ax.plot(xx,y,c=c,ls=ls,marker='o',ms=4,label=l)
    ax.set_xticks(xx,labels);ax.set_ylabel('Measured pipeline throughput (frames/s)');ax.legend(ncol=2,fontsize=8);save(fig,'benchmark')
    fig,ax=setup(1,3);idx=np.arange(4);ax.bar(idx-.18,[14.4,99.2,297.9,565.6],.36,color=BLUE,label='Application')
    ax.bar(idx+.18,[2110.7,2099.5,2768.5,2866.2],.36,color=TEAL,label='Process tree')
    ax.set_xticks(idx,['CPU\nStandard','CPU\nCreative','CUDA\nStandard','CUDA\nCreative']);ax.set_ylabel('Peak host private memory (MiB)');ax.legend(fontsize=8);save(fig,'perf_memory')

CHAPTER_FILES={
1:['README.md','CHANGELOG.md','docs/RELEASE_1.3.0.md'],2:['docs/INPUT_CONTRACT_1.3.0.md','src/Transcode.cpp','src/Media.cpp','docs/COLOR_PIPELINE.md'],
3:['src/color/HLG.cpp','include/logforge/Color.h'],4:['src/color/AppleLog.cpp','include/logforge/Color.h','tests/tests.cpp'],
5:['src/FFmpegNumeric.cpp','src/Transcode.cpp'],6:['src/color/HLG.cpp','docs/CREATIVE_ADJUSTMENTS.md'],7:['src/PixelSanity.cpp','src/FFmpegNumeric.cpp','tests/signal_integrity.py'],
8:['CMakeLists.txt','src/Transcode.cpp','src/Platform.cpp'],9:['src/FloatBridge.cpp','include/logforge/FloatBridge.h'],10:['src/color/FloatTransformer.cpp','tests/hardening.cpp'],
11:['src/color/CudaTransformer.cpp','src/color/ColorKernel.cu','tools/compile_cuda.py','tests/cuda_artifact.py'],12:['docs/CADENCE_1.3.0.md','tests/cadence_v130.cpp','src/Cadence.cpp','src/Media.cpp'],
13:['src/AudioPayload.cpp','src/AudioPayload.cpp','src/MediaSafety.cpp','src/Transcode.cpp','tests/compatibility_v121.py'],14:['src/MovAnalyzer.cpp','tests/hardening.cpp'],
15:['src/AppleLogIdentification.cpp','docs/APPLE_LOG_IDENTIFICATION.md','tests/identification.cpp'],16:['src/MetadataPolicy.cpp','src/MediaSafety.cpp','src/Media.cpp'],
17:['src/ToolTrust.cpp','src/FFmpeg.cpp','src/Discovery.cpp','src/DiscoveryHelper.cpp'],18:['src/Platform.cpp','src/Transcode.cpp','tests/process_fixture.cpp','tests/reliability.cpp'],
19:['src/StorageSafety.cpp','src/Settings.cpp','src/Transcode.cpp','tests/v120.cpp'],20:['src/MainWindow.cpp','src/SettingsWindow.cpp','src/Ui.cpp','src/Localization.cpp','src/Cli.cpp','src/Queue.cpp'],
21:['CMakeLists.txt','tests/integration.py','tests/sustained.py','docs/VALIDATION.md'],22:['docs/BENCHMARK_1.2.0.md','docs/benchmarks/1.2.0.json','tests/benchmark.py'],
23:['src/Transcode.cpp','src/FloatBridge.cpp','src/color/CudaTransformer.cpp'],24:['README.md','docs/RELEASE_1.3.0.md','docs/VALIDATION.md']}
ROLES={
'AppleLog':'Apple Log 分段编解码与公开常数契约；第4章', 'HLG':'HLG 逆变换、参考尺度、创意调整和信号计数；第3、6章',
'FloatTransformer':'持久 CPU 工作线程、tile 配对及统计合并；第10章','CudaTransformer':'动态驱动接口、资格验证和事务提交；第11章',
'FloatBridge':'三槽读算写状态机与线程预算；第9章','Cadence':'全包周期、相位与固定时钟验证；第12章',
'MediaSafety':'时间语义、显示矩阵和固定宽度头修补；第13、16章','Media':'探测解析、准入、元数据参数与输出验证；第2、16章',
'MetadataPolicy':'元数据白名单和冲突分类；第16章','MovAnalyzer':'有界原子解析与类型化语义比较；第14章',
'AppleLogIdentification':'35字节识别扩展写入及结构检查；第15章','FFmpegNumeric':'独立整数信号与实际编解码数值资格；第5、7章',
'FFmpeg':'工具能力、发现调度和固定下载；第17章','ToolTrust':'双可执行文件身份、批准与租约；第17章',
'StorageSafety':'文件身份日志、状态锁与所有权恢复；第19章','Platform':'宽字符、句柄、进程监督、管道与哈希；第18章',
'Transcode':'完整媒体任务、后端、验证报告与最终发布；第8、23章','PixelSanity':'首中尾帧九块编码后粗差检查；第7章',
'Queue':'串行多文件任务及逐项失败记录；第20章','Discovery':'本地卷与目录遍历；第17章','DiscoveryHelper':'受监督隐藏搜索入口与来源预算；第17章',
'Cli':'命令与数值参数、核心自动化入口；第20章','MainWindow':'单后台工作者、事件与可测量布局；第20章',
'SettingsWindow':'设置草稿、保存取消、候选审阅与对话框；第20章','Ui':'主题、DPI、原生绘制与截图；第20章',
'Settings':'字段合并、并发锁及原子偏好保存；第19、20章','Localization':'稳定消息键和单遍参数替换；第20章',
'Color':'颜色模型接口、常数、参数与统计结构；第3至7章',
'tests':'基础数学、媒体和平台测试；第21章','application':'偏好、发现和版本资源测试；第21章',
'hardening':'节奏、工具信任、MOV、并行与发布回归；第21章','reliability':'进程、发现与CLI异常回归；第21章',
'v120':'存储、矩阵、CUDA故障、队列和媒体回归；第21章','identification':'识别写入、幂等与失败结构测试；第15、21章',
'AudioPayload':'每音轨压缩包SHA-256载荷验证；第13章','cadence_v130':'全序列量化单元与时钟回归；第12、21章','v130':'队列、发布及CUDA缓存回归；第11、20、21章','integration':'端到端合成媒体测试；1.3.0 发布记录通过明确冲突与覆盖回归；第21章',
'compatibility_v121':'竖拍像素、矩阵与音频偏移回归；第13、21章','signal_integrity':'独立整数色块、创意与范围计数；第7、21章',
'sustained':'240帧4K120、内存和编码后亮度；第21章','benchmark':'正式吞吐矩阵与过程观测；第22章',
'cuda_artifact':'CUDA源码、嵌入PTX哈希与选项一致性；第11章','fake_ffmpeg':'未知工具不得执行的标记夹具；第17章',
'process_fixture':'后代持管道、长行、退出码夹具；第18章','resolve_identification':'真实Resolve控制导入脚本，本文未重跑；第15章',
'gui_smoke':'交互桌面GUI场景脚本，本文未重跑；第20章','layout_snapshots':'生产布局与主题DPI截图断言；第20章','package_audit':'归档允许列表、CRC、EXE与SHA-256审计；附录',
'compile_cuda':'NVRTC精确选项编译和生成清单；第11章'}

def inventory():
    paths=subprocess.check_output(['git','-c','core.quotepath=false','ls-tree','-r','--name-only',SHA],cwd=REPO,text=True,encoding='utf-8').splitlines()
    rows=[]
    snapshot=zipfile.ZipFile(ROOT/f'LogForge_{SHA[:7]}_源码快照.zip')
    for path in paths:
        if path.startswith('AcademicPapers/'):continue
        b=snapshot.read(path)
        try:t=b.decode('utf-8-sig');lines=len(t.splitlines());kind='text'
        except UnicodeDecodeError:t='';lines=None;kind='binary'
        stem=Path(path).stem
        if path=='src/color/ColorKernel.ptx.h':role='生成PTX字节数组：检查内容哈希及源清单，不视为手写算法；第11章'
        elif path=='src/color/ColorKernel.cu':role='设备端double颜色核与共享内存归约；第11章'
        elif path.endswith('ColorKernel.manifest.json'):role='CUDA源、PTX、编译器身份与精确参数记录；第11章'
        elif path=='third_party/nlohmann/json.hpp':role='第三方JSON 3.12.0单头依赖：JSON读写支持，非项目颜色算法'
        elif 'LICENSE' in path or path.endswith('NOTICE.txt') or path=='THIRD_PARTY_NOTICES.md':role='项目或第三方许可与归属说明；发行边界'
        elif path.endswith('Messages.inc'):role='双语消息宏表、稳定键与占位符；第20章'
        elif path.startswith('docs/') or path in ['README.md','CHANGELOG.md','CONTRIBUTING.md']:role='项目说明、历史证据或开发流程；按章节主题交叉核对'
        elif path.startswith('resources/'):role='图标、清单、版本资源及生成模板；构建与界面'
        elif path.startswith('.github/') or path=='CMakeLists.txt':role='C++20/MSVC目标、测试注册、构建与发布允许列表；第8、21章'
        elif path.startswith('.'):role='仓库格式、换行和忽略规则；复现基础'
        else:role=ROLES.get(stem,'接口或配套实现，纳入源码索引')
        # Read every text file and retain structural index without duplicating the full copyrighted dependency.
        symbols=[]
        for n,line in enumerate(t.splitlines(),1):
            if re.match(r'^(?:[\w:<>&*]+\s+)+[\w:]+\([^;]*|^def |^class |^# ',line):
                symbols.append({'line':n,'text':line[:220]})
        rows.append({'path':path,'bytes':len(b),'lines':lines,'sha256':hashlib.sha256(b).hexdigest(),'kind':kind,'role':role,'symbols':symbols})
    (ROOT/'全仓库源码清单.json').write_text(json.dumps({'commit':SHA,'files':rows},ensure_ascii=False,indent=2),encoding='utf-8')
    (ROOT/'全仓库源码清单.tsv').write_text('path\tlines\tbytes\tsha256\trole\n'+'\n'.join(f"{r['path']}\t{r['lines']}\t{r['bytes']}\t{r['sha256']}\t{r['role']}" for r in rows),encoding='utf-8')
    return rows

def prepare():
    actual_sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=REPO,text=True).strip()
    if actual_sha!=SHA:raise RuntimeError('Repository revision differs from the paper revision: '+actual_sha)
    shutil.copy2(REPO/'docs/verification/1.3.0.json',EVID/'verification_1.3.0.json')
    return inventory()

pdfmetrics.registerFont(TTFont('Song','C:/Windows/Fonts/simsun.ttc',subfontIndex=0))
pdfmetrics.registerFont(TTFont('Hei','C:/Windows/Fonts/simhei.ttf'))
pdfmetrics.registerFont(TTFont('Times','C:/Windows/Fonts/times.ttf'))
pdfmetrics.registerFont(TTFont('TimesBold','C:/Windows/Fonts/timesbd.ttf'))
pdfmetrics.registerFontFamily('Song',normal='Song',bold='Hei',italic='Song',boldItalic='Hei')
PAGEW,PAGEH=A4; LEFT=53; RIGHT=53; WIDTH=PAGEW-LEFT-RIGHT
ST={
'body':ParagraphStyle('body',fontName='Song',fontSize=10.5,leading=16.2,spaceAfter=6,firstLineIndent=21,alignment=TA_JUSTIFY,wordWrap='CJK',textColor=colors.HexColor('#202D35'),allowWidows=0,allowOrphans=0),
'h1':ParagraphStyle('h1',fontName='Hei',fontSize=19,leading=29,spaceBefore=2,spaceAfter=17,textColor=colors.HexColor(NAVY),keepWithNext=True,wordWrap='CJK'),
'h2':ParagraphStyle('h2',fontName='Hei',fontSize=12.2,leading=20,spaceBefore=11,spaceAfter=6,textColor=colors.HexColor(TEAL),keepWithNext=True,wordWrap='CJK'),
'cap':ParagraphStyle('cap',fontName='Song',fontSize=8.6,leading=13,spaceBefore=5,spaceAfter=11,textColor=colors.HexColor(GREY),wordWrap='CJK'),
'small':ParagraphStyle('small',fontName='Song',fontSize=8.5,leading=13.5,spaceAfter=6,wordWrap='CJK',textColor=colors.HexColor(GREY)),
'cell':ParagraphStyle('cell',fontName='Song',fontSize=8.2,leading=12.4,wordWrap='CJK'),
'th':ParagraphStyle('th',fontName='Hei',fontSize=8.4,leading=12.6,textColor=colors.white,wordWrap='CJK'),
'ref':ParagraphStyle('ref',fontName='Song',fontSize=8.4,leading=13.2,spaceAfter=8,wordWrap='CJK'),
}
def p(t,style='body'):return Paragraph(escape(t),ST[style])
class Vector(Flowable):
    def __init__(self,path,maxwidth=WIDTH,maxheight=310):
        Flowable.__init__(self);self.drawing=svg2rlg(str(path));d=self.drawing
        self.scale=min(maxwidth/d.width,maxheight/d.height,1)
        self.width=d.width*self.scale;self.height=d.height*self.scale;self.hAlign='CENTER'
    def draw(self):
        self.canv.saveState();self.canv.scale(self.scale,self.scale);renderPDF.draw(self.drawing,self.canv,0,0);self.canv.restoreState()
def math_normalize(expr):
    expr=expr.replace(r'\land',r'\wedge').replace(r'\bmod',r'\;\mathrm{mod}\;')
    expr=expr.replace(r'\mathsf T',r'\mathsf{T}').replace(r'\bigl','').replace(r'\bigr','')
    expr=re.sub(r'\\frac([0-9])([0-9])',r'\\frac{\1}{\2}',expr)
    expr=re.sub(r'\\xrightarrow\{([^{}]*(?:\{[^{}]*\}[^{}]*)*)\}',r'\\overset{\1}{\\longrightarrow}',expr)
    return expr

class Eq(Flowable):
    def __init__(self,expr,number):
        Flowable.__init__(self);self.number=number
        path=ASSETS/f'equation_{number:03}.svg'
        expr=math_normalize(expr)
        mathtext.math_to_image('$'+expr+'$',str(path),prop=matplotlib.font_manager.FontProperties(size=13),format='svg',color=NAVY)
        self.drawing=svg2rlg(str(path));self.scale=min(1,(WIDTH-50)/self.drawing.width)
        self.width=WIDTH;self.height=max(25,self.drawing.height*self.scale+17)
    def draw(self):
        d=self.drawing;s=self.scale;self.canv.saveState();self.canv.translate((WIDTH-d.width*s)/2,8);self.canv.scale(s,s);renderPDF.draw(d,self.canv,0,0);self.canv.restoreState()
        self.canv.setFont('Times',10);self.canv.setFillColor(colors.HexColor(GREY));self.canv.drawRightString(WIDTH,self.height/2-3,f'({self.number})')
def table(rows,widths=None):
    n=len(rows[0]);widths=widths or [WIDTH/n]*n
    data=[[p(str(x),'th' if i==0 else 'cell') for x in row] for i,row in enumerate(rows)]
    t=Table(data,colWidths=widths,repeatRows=1,hAlign='LEFT')
    t.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,0),colors.HexColor(NAVY)),('VALIGN',(0,0),(-1,-1),'TOP'),
      ('ROWBACKGROUNDS',(0,1),(-1,-1),[colors.white,colors.HexColor('#F1F5F6')]),('LINEBELOW',(0,0),(-1,0),.7,colors.HexColor(TEAL)),
      ('LINEBELOW',(0,1),(-1,-1),.3,colors.HexColor('#D6E0E5')),('LEFTPADDING',(0,0),(-1,-1),7),('RIGHTPADDING',(0,0),(-1,-1),7),
      ('TOPPADDING',(0,0),(-1,-1),6),('BOTTOMPADDING',(0,0),(-1,-1),6)]))
    return t

class Paper(BaseDocTemplate):
    def __init__(self,path):
        super().__init__(str(path),pagesize=A4,leftMargin=LEFT,rightMargin=RIGHT,topMargin=57,bottomMargin=51,
          title='LogForge：HLG至Apple Log转换的数学原理、源码实现与验证',author='源码研究与技术整理',subject='固定提交913e4b9；版本1.3.0；中文技术论文',pageCompression=1)
        f=Frame(LEFT,51,WIDTH,PAGEH-108,id='normal',leftPadding=0,rightPadding=0,topPadding=0,bottomPadding=0)
        self.addPageTemplates([PageTemplate(id='cover',frames=[f],onPage=self.coverpage),PageTemplate(id='main',frames=[f],onPage=self.header)])
        self.section='';self.hcount=0
    def beforeDocument(self):self.section='';self.hcount=0
    def coverpage(self,c,doc):pass
    def header(self,c,doc):
        c.saveState();c.setStrokeColor(colors.HexColor(TEAL));c.setLineWidth(.55);c.line(LEFT,PAGEH-35,PAGEW-RIGHT,PAGEH-35)
        c.setFont('TimesBold',8);c.setFillColor(colors.HexColor(NAVY));c.drawString(LEFT,PAGEH-27,'LOGFORGE  |  SOURCE & MATHEMATICS')
        c.setFont('Song',7.5);c.drawRightString(PAGEW-RIGHT,PAGEH-27,'1.3.0 · 26929A · 913e4b9')
        c.setStrokeColor(colors.HexColor('#CBD6DC'));c.line(LEFT,36,PAGEW-RIGHT,36)
        c.setFont('Song',7.4);c.setFillColor(colors.HexColor(GREY));c.drawString(LEFT,24,'固定版本研究  ·  2026-09-29')
        c.setFont('Times',9);c.drawRightString(PAGEW-RIGHT,24,str(doc.page));c.restoreState()
    def afterFlowable(self,flow):
        if isinstance(flow,Paragraph) and flow.style.name=='h1':
            text=flow.getPlainText();key=f'section_{self.hcount}';self.hcount+=1;self.canv.bookmarkPage(key)
            self.canv.addOutlineEntry(text,key,0,False)
            if text not in ['目录','图表说明与证据约定']:
                self.notify('TOCEntry',(0,text,self.page,key))

def refs():
    return [
    ('项目固定版本源码与README',f'https://github.com/HongHuo0728/LogForge/tree/{SHA}'),
    ('LogForge COLOR_PIPELINE：数值契约',BASE+'docs/COLOR_PIPELINE.md'),
    ('LogForge CREATIVE_ADJUSTMENTS：项目自定义调整',BASE+'docs/CREATIVE_ADJUSTMENTS.md'),
    ('LogForge ARCHITECTURE：架构说明，含历史政策',BASE+'docs/ARCHITECTURE.md'),
    ('LogForge RELEASE_1.3.0：当前准入与像素方向策略',BASE+'docs/RELEASE_1.3.0.md'),
    ('LogForge VALIDATION：版本化历史验证记录',BASE+'docs/VALIDATION.md'),
    ('LogForge BENCHMARK_1.2.0：历史性能矩阵',BASE+'docs/BENCHMARK_1.2.0.md'),
    ('LogForge APPLE_LOG_IDENTIFICATION：原生参考与Resolve受控实验',BASE+'docs/APPLE_LOG_IDENTIFICATION.md'),
    ('LogForge METADATA 与 FFMPEG_PROVIDER：容器与工具政策',BASE+'docs/METADATA.md'),
    ('LogForge TECHNICAL_REFERENCES：外部原始参考索引',BASE+'docs/TECHNICAL_REFERENCES.md'),
    ('Apple提供的ACES IDT，固定提交528c78fe：Apple Log解码常数', 'https://github.com/ampas/aces-dev/blob/528c78fe2c0f4e7eb322581e98aba05de79466cb/transforms/ctl/idt/vendorSupplied/apple/IDT.Apple.AppleLog_BT2020.ctl'),
    ('ITU-R BT.2100-3 (2025)：HLG传递函数；本次核对官方版本索引','https://www.itu.int/rec/R-REC-BT.2100'),
    ('ITU-R BT.2408-6：HDR制作参考；本文尺度依据仓库固定数值契约交叉说明','https://www.itu.int/dms_pub/itu-r/opb/rep/R-REP-BT.2408-6-2023-PDF-E.pdf'),
    ('ITU-R BT.2020：超高清电视基色与矩阵','https://www.itu.int/rec/R-REC-BT.2020'),
    ('FFmpeg官方滤镜手册：zscale；具体运行行为由资格测试约束','https://ffmpeg.org/ffmpeg-filters.html#zscale'),
    ('FFmpeg n8.1.2 MOV封装源文件：仓库引用的固定版本','https://github.com/FFmpeg/FFmpeg/blob/n8.1.2/libavformat/movenc.c'),
    ('Apple QuickTime File Format：矩阵与容器原始文档','https://developer.apple.com/documentation/quicktime-file-format/matrices'),
    ('NVIDIA NVRTC 12.8.1：生成PTX所用工具接口参考','https://docs.nvidia.com/cuda/archive/12.8.1/nvrtc/index.html'),
    ('Microsoft Windows：Job Objects监督接口参考','https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects'),
    ('CMake CPack：归档与校验和生成参考','https://cmake.org/cmake/help/latest/module/CPack.html')]

def build(rows):
    source=(ROOT/'论文正文.md').read_text(encoding='utf-8')
    cjk=len(re.findall('[\u4e00-\u9fff]',source)); body=source.split('# 附录 A')[0];bodycjk=len(re.findall('[\u4e00-\u9fff]',body))
    story=[]; tex=[]; eqnum=0; fignum=0; tabnum=0; chapter=0;figs=[]
    coverstyle=ParagraphStyle('cover',fontName='Hei',fontSize=31,leading=43,textColor=colors.HexColor(NAVY),wordWrap='CJK')
    story += [Spacer(1,70),p('SOURCE CODE MONOGRAPH','small'),Spacer(1,15),Paragraph('LogForge',ParagraphStyle('brand',fontName='TimesBold',fontSize=48,leading=55,textColor=colors.HexColor(TEAL))),Spacer(1,20),Paragraph('HLG 至 Apple Log 转换的<br/>数学原理、源码实现与验证',coverstyle),Spacer(1,25),p('从颜色传递函数到可靠媒体交付的逐层论证','h2'),Spacer(1,25),table([
      ['研究对象','固定版本与证据'],['仓库','HongHuo0728 / LogForge'],['版本','1.3.0（26929A）'],['提交',SHA],['完成日期','2026 年 9 月 30 日'],['正文规模',f'{bodycjk:,} 个中文汉字（含摘要，不计公式、英文和附录）'],['验证摘要','Release 构建通过；29 项 CTest：29 通过、0 失败'],['证据政策','源码事实、数学推导、历史测量和发布验证分别标识']], [100,WIDTH-100]),Spacer(1,26),p('本文为公开源码的技术研究稿。未虚构作者单位、学术发表、相机认证或商业编辑器兼容性。','small'),NextPageTemplate('main'),PageBreak()]
    story.append(p('目录','h1'));toc=TableOfContents(tableStyle=TableStyle([('LEFTPADDING',(0,0),(-1,-1),0),('RIGHTPADDING',(0,0),(-1,-1),0),('TOPPADDING',(0,0),(-1,-1),0),('BOTTOMPADDING',(0,0),(-1,-1),0)]));toc.levelStyles=[ParagraphStyle('toc0',fontName='Song',fontSize=10,leading=21,leftIndent=0,firstLineIndent=0,spaceBefore=0,textColor=colors.HexColor(NAVY))];story.extend([toc,PageBreak()])
    story.extend([p('图表说明与证据约定','h1'),p('本文的数学曲线按公开常数和当前源码公式直接计算。架构图与状态图为实现关系的抽象示意。标明“历史”的性能图使用仓库 1.2.0 数据；持续内存图保留 1.2.1 历史记录；1.3.0 测试表引用 2026 年 9 月 29 日发布 JSON。所有图均不使用未经说明的虚构实验数据。'),p('正文中的公式连续编号，图和表分别连续编号。每章末给出关键源码定位，完整逐文件范围见附录 B 和配套清单。源码超链接固定到提交，避免主分支后续变化。'),p('阅读路径：第一至七章建立颜色与数值模型；第八至十二章解释执行和时钟；第十三至十九章解释媒体语义与可靠性；第二十至二十四章连接界面、复测、性能、实现演练和研究结论。'),PageBreak()])
    def texesc(t):
        return ''.join({'\\':r'\textbackslash{}','&':r'\&','%':r'\%','$':r'\$','#':r'\#','_':r'\_','{':r'\{','}':r'\}','~':r'\textasciitilde{}','^':r'\textasciicircum{}'}.get(c,c) for c in t)
    def citations(n):
        if n not in CHAPTER_FILES:return
        text='本章主要源码依据：'+ '；'.join(CHAPTER_FILES[n])+'。均为固定提交 913e4b9。'
        links='本章主要源码依据：'+'；'.join(f'<link href="{BASE+f}" color="{TEAL}">{escape(f)}</link>' for f in CHAPTER_FILES[n])+'。'
        last=story.pop();group=[last,Spacer(1,8),Paragraph(links,ST['small'])]
        if story and isinstance(story[-1],Paragraph) and story[-1].style.name=='h2':group.insert(0,story.pop())
        story.append(KeepTogether(group));tex.append('\n\\par\\small '+texesc(text)+'\\normalsize\n')
    lines=source.splitlines();i=0
    while i<len(lines):
        l=lines[i].strip();i+=1
        if not l:continue
        if l.startswith('# '):
            citations(chapter);title=l[2:]
            if chapter or title not in ['摘要']:story.append(PageBreak())
            if title.startswith('第') and '章' in title:chapter+=1
            else:chapter=chapter if title.startswith('Abstract') else (0 if title.startswith('附录') or title.startswith('参考') else chapter)
            story.append(p(title,'h1'));tex.append('\n\\chapter*{'+texesc(title)+'}\\addcontentsline{toc}{chapter}{'+texesc(title)+'}\n')
        elif l.startswith('## '):story.append(p(l[3:],'h2'));tex.append('\n\\section*{'+texesc(l[3:])+'}\n')
        elif l.startswith('$$'):
            eqnum+=1;ex=l[2:-2];story.append(Eq(ex,eqnum));tex.append('\n\\begin{equation}\n'+ex+'\n\\end{equation}\n')
        elif l.startswith('@figure:'):
            key,caption=l[8:].split('|',1);fignum+=1;figs.append({'number':fignum,'id':key,'caption':caption})
            v=Vector(ASSETS/f'{key}.svg');story.append(KeepTogether([Spacer(1,8),v,p(f'图 {fignum}　{caption}','cap')]))
            tex.append('\n\\begin{figure}[htbp]\\centering\\includegraphics[width=\\linewidth]{figures/'+key+'.pdf}\\caption{'+texesc(caption)+'}\\end{figure}\n')
        elif l.startswith('@table:'):
            cap=l[7:];data=[]
            while i<len(lines) and lines[i].strip()!='@end':data.append(lines[i].strip().split('|'));i+=1
            i+=1;tabnum+=1;story +=[p(f'表 {tabnum}　{cap}','cap'),table(data),Spacer(1,8)]
            tex.append('\n\\begin{longtable}{'+''.join('p{'+str(round(.88/len(data[0]),3))+'\\linewidth}' for _ in data[0])+'}\n\\caption{'+texesc(cap)+'}\\\\\n\\toprule\n'+'\\\\\n'.join(' & '.join(texesc(x) for x in row) for row in data)+'\\\\\n\\bottomrule\n\\end{longtable}\n')
        elif l=='@results':
            xr=ET.parse(ROOT/'本次回归测试.xml').getroot();data=[['测试名称','结果','耗时／秒']]
            for x in xr.iter('testcase'):data.append([x.attrib['name'],'失败' if x.find('failure') is not None else '通过',f"{float(x.attrib.get('time',0)):.3f}"])
            tabnum+=1;story.extend([p(f'表 {tabnum}　1.3.0 发布记录的完整 CTest 结果','cap'),table(data,[WIDTH*.63,WIDTH*.17,WIDTH*.20]),Spacer(1,8)])
            tex.append('\n\\begin{longtable}{p{.60\\linewidth}rr}\\toprule\n'+'\\\\\n'.join(' & '.join(texesc(v) for v in row) for row in data)+'\\\\\\bottomrule\\end{longtable}\n')
        elif l=='@inventory':
            tabnum+=1;story.append(p(f'表 {tabnum}　受控文件范围，共 {len(rows)} 项','cap'))
            data=[['文件路径与行数','实现责任／研究用途']]
            for r in rows:data.append([r['path']+'\n'+(f"{r['lines']} 行" if r['lines'] is not None else f"二进制 {r['bytes']} 字节"),r['role']])
            inventory_table=table(data,[WIDTH*.45,WIDTH*.55]);inventory_table.setStyle(TableStyle([('TOPPADDING',(0,0),(-1,-1),5.5),('BOTTOMPADDING',(0,0),(-1,-1),5.5)]));story.append(inventory_table)
            tex.append('\n\\begin{longtable}{p{.43\\linewidth}p{.47\\linewidth}}\\toprule\n'+'\\\\\n'.join(' & '.join(texesc(v) for v in row) for row in data)+'\\\\\\bottomrule\\end{longtable}\n')
        elif l=='@references':
            for n,(name,url) in enumerate(refs(),1):
                if n==11:story.append(PageBreak());story.append(p('参考文献与源码定位（续）','h2'))
                story.append(Paragraph(f'[{n}] {escape(name)}。<br/><link href="{escape(url)}" color="{TEAL}">{escape(url)}</link>',ST['ref']))
                tex.append('\\par ['+str(n)+'] '+texesc(name)+'. \\url{'+url+'}\\par\n')
        else:story.append(p(l));tex.append(texesc(l)+'\n\n')
    doc=Paper(PDF);doc.multiBuild(story)
    preamble=r'''\documentclass[UTF8,zihao=-4,openany]{ctexbook}
\usepackage[a4paper,margin=24mm]{geometry}
\usepackage{amsmath,amssymb,booktabs,longtable,graphicx,xcolor,hyperref,fancyhdr}
\definecolor{navy}{HTML}{163247}
\hypersetup{colorlinks=true,linkcolor=navy,urlcolor=navy}
\setlength{\parskip}{5pt}
\pagestyle{fancy}\fancyhf{}\fancyhead[L]{LogForge 源码研究与数学论证}\fancyfoot[C]{\thepage}
\title{LogForge：HLG 至 Apple Log 转换的数学原理、源码实现与验证}
\author{公开源码技术研究稿}\date{2026年9月30日}
\begin{document}\maketitle\tableofcontents
'''
    (ROOT/'LogForge_技术论文.tex').write_text(preamble+''.join(tex)+'\n\\end{document}\n',encoding='utf-8')
    (ROOT/'图表目录.json').write_text(json.dumps(figs,ensure_ascii=False,indent=2),encoding='utf-8')
    pdfdoc=fitz.open(PDF);pages=len(pdfdoc)
    metrics={'commit':SHA,'version':'1.3.0 (26929A)','body_cjk_characters':bodycjk,'manuscript_cjk_characters':cjk,
      'manuscript_characters':len(source),'pages':pages,'equations':eqnum,'figures':fignum,'tables':tabnum,'tracked_files':len(rows),
      'pdf_sha256':hashlib.sha256(PDF.read_bytes()).hexdigest(),'ctest':{'tests':29,'passed':29,'failed':0,'provenance':'docs/verification/1.3.0.json; existing release record'},
      'latex_builtin_compile':'not run: multi-file project uses ReportLab export','pdf_export':'ReportLab; embedded CJK fonts; vector SVG math and charts'}
    (ROOT/'制作与验证统计.json').write_text(json.dumps(metrics,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(metrics,ensure_ascii=False,indent=2))
    return pdfdoc

if __name__=='__main__':
    rows=prepare();figures();build(rows)
