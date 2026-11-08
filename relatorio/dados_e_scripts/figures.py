import importlib.util as u, numpy as np, json, statistics as st
import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
sp=u.spec_from_file_location('an','analysis.py'); a=u.module_from_spec(sp); sp.loader.exec_module(a)
B,O,G='#2a78d6','#eb6834','#1baf7a'; GR='#8a8984'; INK='#0b0b0b'; SEC='#52514e'; GRID='#e6e5e0'
plt.rcParams.update({'mathtext.default':'regular','font.family':'Liberation Sans','font.size':8,'axes.edgecolor':'#bdbcb6','axes.linewidth':.6,'axes.labelcolor':SEC,'xtick.color':SEC,'ytick.color':SEC,'xtick.major.size':2.5,'ytick.major.size':2.5,'axes.grid':True,'grid.color':GRID,'grid.linewidth':.5,'axes.axisbelow':True,'axes.spines.top':False,'axes.spines.right':False,'legend.frameon':False,'legend.fontsize':7,'legend.handlelength':2.0,'legend.labelspacing':.25,'axes.titlesize':8.5,'axes.titleweight':'bold','axes.titlelocation':'left','axes.titlecolor':INK})
Lf=st.median(a.frames(997)[1:-1])
def classic(C,prop,fer=0,chunk=997):
    L=st.median(a.frames(chunk)[1:-1]); Tf=L*10/C; return (1-fer)/(1+2*prop/Tf)
fig,ax=plt.subplots(2,2,figsize=(17/2.54,11.6/2.54))
M=dict(ms=4.5,lw=0,mec='white',mew=.5,zorder=5)
EB=dict(lw=0,elinewidth=.8,capsize=2,zorder=4)
# (a) FER
A=ax[0,0]; g=a.grp('fer','ber'); bers=np.array([0,1e-5,2e-5,5e-5,1e-4,1.5e-4,2e-4,3e-4])
xs=np.linspace(0,3.2e-4,80); pts=[a.predict(9600,0,b,997) for b in xs]
A.plot([p[2] for p in pts],[p[0] for p in pts],color=B,lw=1.6,label='Modelo refinado')
f=np.linspace(0,.9,10); A.plot(f,1-f,color=GR,lw=1.2,ls='--',label='Clássica: 1 − FER')
ss=[a.summ(v) for v in g.values()]
A.errorbar([s['fer'] for s in ss],[s['S'] for s in ss],yerr=[s['Ss'] for s in ss],fmt='o',color=B,ecolor=B,label='Medido (±σ)',**{k:v for k,v in M.items() if k!='lw'},elinewidth=.8,capsize=2)
A.set(xlabel='FER medido',ylabel='Eficiência S',title='(a) Taxa de erros de trama',xlim=(-.02,.92),ylim=(0,1.03)); A.legend(loc='upper right')
# (b) a
A=ax[0,1]; aa=np.logspace(-2.2,1.2,80)
A.plot(aa,1/(1+2*aa),color=GR,lw=1.2,ls='--',label='Clássica: 1/(1+2a)')
for e,C,c,m,lab in (('prop9600',9600,B,'o','C = 9600 bit/s'),('prop115200',115200,O,'s','C = 115200 bit/s')):
    g=a.grp(e,'prop_us'); x=[];y=[]
    for k,v in g.items():
        if k==0: continue
        x.append(k/1e6*C/(10*Lf)); y.append(a.summ(v)['S'])
    tp=np.logspace(-3,0,60); A.plot(tp*C/(10*Lf),[a.predict(C,t,0,997)[0] for t in tp],color=c,lw=1.6)
    A.plot(x,y,m,color=c,label=lab,**M)
A.set_xscale('log'); A.set(xlabel='a = Tprop / Tf',ylabel='Eficiência S',title='(b) Atraso de propagação',ylim=(0,1.03),xlim=(.006,16)); A.legend(loc='upper right',title_fontsize=7,title='Traço cheio: modelo refinado')
# (c) C
A=ax[1,0]; Cs=[1200,2400,4800,9600,19200,38400,57600,115200]
for e,p,c,m,lab in (('baud0',0,B,'o','Tprop = 0'),('baud100ms',.1,O,'s','Tprop = 100 ms')):
    g=a.grp(e,'baud'); xx=np.logspace(np.log10(1200),np.log10(115200),40)
    A.plot(xx,[a.predict(x,p,0,997,Tout=99)[0] for x in xx],color=c,lw=1.6)
    A.plot(xx,[classic(x,p) for x in xx],color=c,lw=1,ls='--',alpha=.65)
    s={k:a.summ(v) for k,v in g.items()}
    A.plot([k for k in s if s[k]['nok']],[s[k]['S'] for k in s if s[k]['nok']],m,color=c,label=lab,**M)
A.plot([1200,2400],[0.012,0.012],'x',color=INK,ms=5.5,mew=1.2,clip_on=False,zorder=6,label='Falha (Tout = 4 s)')
g=a.grp('baudT30','baud'); A.plot(list(g),[a.summ(v)['S'] for v in g.values()],'o',mfc='white',mec=B,mew=1,ms=5,lw=0,zorder=6,label='Tout = 30 s')
A.set_xscale('log'); A.set_xticks([1200,4800,19200,115200]); A.set_xticklabels(['1200','4800','19200','115200']); A.minorticks_off()
A.set(xlabel='Capacidade C (bit/s)',ylabel='Eficiência S',title='(c) Capacidade da ligação',ylim=(0,1.03)); A.legend(loc='lower left',ncol=1,bbox_to_anchor=(.0,.06),title='Tracejado: clássica',title_fontsize=7)
# (d) L
A=ax[1,1]; ch=np.array([47,60,75,97,130,197,280,397,520,697,850,997])
for e,p,b,c,m,lab in (('L0',0,0,B,'o','BER = 0, Tprop = 0'),('Lprop',.1,0,O,'s','Tprop = 100 ms'),('Lber',0,1e-4,G,'D',r'BER = $10^{-4}$')):
    A.plot(ch+3,[a.predict(9600,p,b,int(k))[0] for k in ch],color=c,lw=1.6)
    if b: A.plot(ch+3,[a.predict(9600,p,b,int(k),timeouts=False)[0] for k in ch],color=c,lw=1,ls=':',label='Modelo sem timeouts')
    g=a.grp(e,'chunk'); s=[a.summ(v) for v in g.values()]; xx=[k+3 for k in g]
    if b: A.errorbar(xx,[q['S'] for q in s],yerr=[q['Ss'] for q in s],fmt=m,color=c,label=lab,ms=4,mec='white',mew=.5,elinewidth=.8,capsize=2,zorder=5)
    else: A.plot(xx,[q['S'] for q in s],m,color=c,label=lab,**M)
A.set_xscale('log'); A.set_xticks([50,100,200,400,1000]); A.set_xticklabels(['50','100','200','400','1000']); A.minorticks_off()
A.set(xlabel='Tamanho do campo de dados L (bytes)',ylabel='Eficiência S',title='(d) Tamanho da trama',ylim=(0,1.03)); A.legend(loc='upper left',ncol=2,columnspacing=.8)
fig.tight_layout(pad=.4,h_pad=.9,w_pad=1.0)
fig.savefig('fig.pdf'); fig.savefig('fig.png',dpi=300)
# numeros
print('Lf mediana',Lf,'Tf',Lf*10/9600,'frames',a.frames(997))
print('bytes totais',sum(a.frames(997)),'stuff',sum(x in(0x7e,0x7d) for x in a.d),'n frames',len(a.frames(997)))
print('\nFER: BER FERm FERp T Ts S Ss Sp classic to corrupt')
tc=0;tn=0
for k,v in a.grp('fer','ber').items():
    s=a.summ(v); p=a.predict(9600,0,k,997); tc+=s['corrupt'];tn+=s['nok']
    print(f"{k:.1e} {s['fer']:.3f} {p[2]:.3f} {s['T']:.2f} {s['Ts']:.2f} R={s['R']:.0f} S={s['S']:.3f} {s['Ss']:.3f} {p[0]:.3f} cl={1-s['fer']:.3f} to={s['to']:.1f}/{p[3]:.1f} c={s['corrupt']}/{s['nok']}")
print('\nprop');
for e,C in (('prop9600',9600),('prop115200',115200)):
    for k,v in a.grp(e,'prop_us').items():
        s=a.summ(v);p=a.predict(C,k/1e6,0,997); print(e,k//1000,f"a={k/1e6*C/(10*Lf):.3f} T={s['T']:.3f} S={s['S']:.4f} pred={p[0]:.4f} cl={classic(C,k/1e6):.3f} err={100*(s['S']/p[0]-1):+.2f}%")
print('\nbaud')
for e,p_ in (('baud0',0),('baud100ms',.1),('baudT30',0)):
    for k,v in a.grp(e,'baud').items():
        s=a.summ(v)
        if s['nok']: p=a.predict(k,p_,0,997,Tout=99); print(e,k,f"T={s['T']:.3f} R={s['R']:.0f} S={s['S']:.4f} pred={p[0]:.4f} cl={classic(k,p_):.3f} err={100*(s['S']/p[0]-1):+.2f}%")
        else: print(e,k,'FALHA',s['fail'],'/',s['n'],'Tfmax',max(a.frames(997))*10/k)
print('\nL')
for e,p_,b in (('L0',0,0),('Lprop',.1,0),('Lber',0,1e-4)):
    for k,v in a.grp(e,'chunk').items():
        s=a.summ(v);p=a.predict(9600,p_,b,k);q=a.predict(9600,p_,b,k,timeouts=False)
        print(e,k+3,f"T={s['T']:.2f}±{s['Ts']:.2f} S={s['S']:.4f}±{s['Ss']:.3f} pred={p[0]:.4f} sem_to={q[0]:.3f} FERm={s['fer']:.3f}/{p[2]:.3f} to={s['to']:.1f}/{p[3]:.1f} c={s['corrupt']}/{s['nok']} err={100*(s['S']/p[0]-1):+.1f}%")
allr=[r for r in a.rows]; print('\ntotal',len(allr),'falhas',sum(r['failed'] for r in allr),'corrupt_ok',sum((not r['failed']) and (not r['intact']) for r in allr),'ber>0 ok',sum((not r['failed']) and r['ber']>0 for r in allr))
errs=[abs(a.summ(v)['S']/a.predict(C,k/1e6,0,997)[0]-1) for e,C in (('prop9600',9600),('prop115200',115200)) for k,v in a.grp(e,'prop_us').items()]; print('max err prop',max(errs))
