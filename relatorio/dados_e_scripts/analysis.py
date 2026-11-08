import csv, collections, math, json, sys
import numpy as np
PG='/home/leonor/Desktop/RCOM/FEUP-RCOM-2026/lab1-code-2627/penguin.gif'
d=open(PG,'rb').read(); FS=len(d)
NAME=PG.encode()
def st(b): return sum(2 if x in (0x7e,0x7d) else 1 for x in b)
def frame_len(p):
    b=0
    for x in p: b^=x
    return 4+st(p)+st(bytes([b]))+1
def frames(chunk):
    """comprimentos exatos em bytes das tramas I da transferencia"""
    ctl=lambda c: bytes([c,0,4])+FS.to_bytes(4,'big')+bytes([1,len(NAME)])+NAME
    L=[frame_len(ctl(1))]
    for i in range(0,FS,chunk):
        c=d[i:i+chunk]; L.append(frame_len(bytes([2,len(c)>>8,len(c)&255])+c))
    L.append(frame_len(ctl(3)))
    return L
def classic(C,prop,fer,chunk):
    """S=(1-FER)/(1+2a), a=Tprop/Tf; Tf da trama de dados tipica"""
    Lf=np.mean(frames(chunk)[1:-1]); Tf=Lf*10/C; a=prop/Tf
    return (1-fer)/(1+2*a), a, Tf
def refined(C,prop,fer,chunk,timeouts=0,Tout=4):
    F=frames(chunk); Ta=5*10/C
    T=(5+5)*10/C+2*prop     # SET/UA
    for L in F: T+=(L*10/C+Ta+2*prop)/(1-fer)
    T+=timeouts*Tout
    return 8*FS/T/C, T
rows=list(csv.DictReader(open('results.csv')))
for r in rows:
    for k in ('baud','prop_us','chunk','timeout'): r[k]=int(r[k])
    r['ber']=float(r['ber']); r['failed']=int(r['failed']); r['intact']=int(r['intact'])
    if not r['failed']:
        r['T']=float(r['elapsed']); r['R']=8*FS/r['T']; r['S']=r['R']/r['baud']
        r['fer_m']=1-int(r['i_acc'])/int(r['i_sent']) if r['i_acc'] else None
        r['to']=int(r['timeouts'])
def grp(exp,key):
    g=collections.defaultdict(list)
    for r in rows:
        if r['exp']==exp: g[r[key]].append(r)
    return dict(sorted(g.items()))
def summ(v):
    ok=[x for x in v if not x['failed']]
    if not ok: return dict(n=len(v),nok=0,fail=len(v))
    S=[x['S'] for x in ok]; T=[x['T'] for x in ok]
    fm=[x['fer_m'] for x in ok if x['fer_m'] is not None]
    return dict(n=len(v),nok=len(ok),fail=len(v)-len(ok),S=np.mean(S),Ss=np.std(S,ddof=1) if len(S)>1 else 0,T=np.mean(T),Ts=np.std(T,ddof=1) if len(T)>1 else 0,
      R=np.mean([x['R'] for x in ok]),fer=np.mean(fm) if fm else 0,to=np.mean([x['to'] for x in ok]),corrupt=sum(not x['intact'] for x in ok),base=ok[0])
OUT={}
if __name__=='__main__':
    F=frames(997); print('FS',FS,'frames',F, 'stuffed extra',sum(F))
    print('\n# FER'); 
    for k,v in grp('fer','ber').items():
        s=summ(v); Lf=np.mean(frames(997)[1:-1]); ft=1-(1-k)**(8*Lf)
        c,_,_=classic(9600,0,s['fer'],997); rf,Tm=refined(9600,0,s['fer'],997,s['to'])
        print(f"BER {k:.0e} n={s['n']} FERm={s['fer']:.3f} FERt={ft:.3f} T={s['T']:.2f}±{s['Ts']:.2f} S={s['S']:.4f}±{s['Ss']:.4f} classic={c:.3f} ratio_ovh={rf:.4f} to={s['to']:.1f} corrupt={s['corrupt']}/{s['nok']}")
    for e,C in (('prop9600',9600),('prop115200',115200)):
        print('\n#',e)
        for k,v in grp(e,'prop_us').items():
            s=summ(v); c,a,Tf=classic(C,k/1e6,0,997); rf,Tm=refined(C,k/1e6,0,997)
            print(f"Tp {k/1000:.0f}ms a={a:.3f} T={s['T']:.3f}±{s['Ts']:.3f} S={s['S']:.4f} classic={c:.4f} refined={rf:.4f} err={100*(s['S']-rf)/rf:+.1f}% to={s['to']}")
    for e,p in (('baud0',0),('baud100ms',.1)):
        print('\n#',e)
        for k,v in grp(e,'baud').items():
            s=summ(v)
            c,a,Tf=classic(k,p,0,997); rf,Tm=refined(k,p,0,997)
            if not s['nok']: print(f"C {k} Tf={Tf:.2f} a={a:.3f} FALHA {s['fail']}/{s['n']} classic={c:.3f} refined={rf:.3f}"); continue
            print(f"C {k} Tf={Tf:.3f} a={a:.3f} T={s['T']:.3f} S={s['S']:.4f}±{s['Ss']:.4f} R={s['R']:.0f} classic={c:.4f} refined={rf:.4f} err={100*(s['S']-rf)/rf:+.1f}% to={s['to']} fail={s['fail']}")
    print('\n# baudT30')
    for k,v in grp('baudT30','baud').items():
        s=summ(v); rf,_=refined(k,0,0,997); print(k,f"n={s['nok']}/{s['n']} T={s['T']:.2f} S={s['S']:.4f} refined={rf:.4f} to={s['to']}")
    for e,p,b in (('L0',0,0),('Lprop',.1,0),('Lber',0,1e-4)):
        print('\n#',e)
        for k,v in grp(e,'chunk').items():
            s=summ(v); Lf=np.mean(frames(k)[1:-1]); ft=1-(1-b)**(8*Lf)
            fer=s['fer'] if b else 0
            c,a,Tf=classic(9600,p,fer,k); rf,Tm=refined(9600,p,fer,k,s['to'])
            rft,_=refined(9600,p,ft,k)
            print(f"L={k+3} Lf={Lf:.1f} nfr={len(frames(k))} T={s['T']:.2f}±{s['Ts']:.2f} S={s['S']:.4f}±{s['Ss']:.4f} FERm={s['fer']:.3f} FERt={ft:.3f} classic={c:.3f} ref={rf:.4f} refFERt={rft:.4f} to={s['to']:.1f} corrupt={s['corrupt']}/{s['nok']}")

def pa_model(Lf,ber):
    """P(aceite, sem timeout) por tentativa: BCC2 XOR em 8 colunas + cabecalho/flag/RR intactos"""
    q=1-(1-ber)**8; h=1-(1-q)**10
    lam=ber*max(Lf-5,0)
    return ((1+math.exp(-2*lam))/2)**8*(1-h), h
def predict(C,prop,ber,chunk,Tout=4,timeouts=True):
    """Modelo sem parametros medidos; devolve S, T, FER agregado, timeouts esperados"""
    F=frames(chunk); Ta=50/C; T=100/C+2*prop; att=0; to=0
    for L in F:
        pa,h=pa_model(L,ber); A=1/pa; cyc=L*10/C+Ta+2*prop
        if not timeouts: h=0
        T+=A*((1-h)*cyc+h*Tout); att+=A; to+=A*h
    return 8*FS/T/C,T,1-len(F)/att,to
