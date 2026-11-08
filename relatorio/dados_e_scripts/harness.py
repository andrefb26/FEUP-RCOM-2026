#!/usr/bin/env python3
"""Banco de ensaios: corre transferencias reais do protocolo sobre o cable.c e grava CSV.
Uso: harness.py <saida.csv> <workers> <plano: smoke|full>"""
import os, re, sys, csv, time, signal, random, shutil, subprocess, threading, queue, hashlib
HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = '/home/leonor/Desktop/RCOM/FEUP-RCOM-2026/lab1-code-2627'
SOCAT = os.path.join(HERE, '..', 'socat', 'x', 'usr', 'bin')
BUILD = os.path.join(HERE, 'build')
FILE = os.path.join(PROJ, 'penguin.gif')
FSIZE = os.path.getsize(FILE)
FHASH = hashlib.md5(open(FILE, 'rb').read()).hexdigest()
lock = threading.Lock()

def build_main(chunk, timeout):
    """Copia src/, acrescenta so o cronometro e os parametros variados."""
    name = f'main_{chunk}_{timeout}'
    exe = os.path.join(BUILD, name)
    with lock:
        if os.path.exists(exe):
            return exe
        d = os.path.join(BUILD, 'src_' + name)
        shutil.rmtree(d, ignore_errors=True)
        shutil.copytree(os.path.join(PROJ, 'src'), d)
        p = os.path.join(d, 'application_layer.c')
        s = open(p).read()
        s = s.replace('#define CHUNK_SIZE 997', f'#define CHUNK_SIZE {chunk}')
        s = s.replace('#include <stdio.h>', '#include <stdio.h>\n#include <time.h>\nstatic double now_(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}', 1)
        a = 'if (llOpenTx(llParameters) < 0) { fclose(f); return; }'
        assert a in s
        s = s.replace(a, 'double t0_ = now_();\n        ' + a)
        b = '        llCloseTx();\n'
        assert b in s
        s = s.replace(b, '        printf("ELAPSED %.6f\\n", now_() - t0_);\n        fflush(stdout);\n' + b)
        open(p, 'w').write(s)
        m = os.path.join(d, 'main.c')
        t = open(m).read()
        assert '#define TIMEOUT 4' in t
        open(m, 'w').write(t.replace('#define TIMEOUT 4', f'#define TIMEOUT {timeout}'))
        c = [os.path.join(d, f) for f in os.listdir(d) if f.endswith('.c')]
        subprocess.check_call(['gcc', '-O0', '-w', '-o', exe] + c)
    return exe

class Cable:
    """cable.c original; so os caminhos das portas mudam, para correr varias instancias."""
    def __init__(self, w):
        self.w = w
        self.tx, self.rx = f'/tmp/rcw{w}Tx', f'/tmp/rcw{w}Rx'
        src = open(os.path.join(PROJ, 'cable', 'cable.c')).read()
        src = src.replace('"/tmp/ttyS10"', f'"{self.tx}"').replace('"/tmp/ttyS11"', f'"{self.rx}"')
        src = src.replace('"/tmp/emulatorTx"', f'"/tmp/rcw{w}eTx"').replace('"/tmp/emulatorRx"', f'"/tmp/rcw{w}eRx"')
        m_ = 'int main(int argc, char *argv[])\n{\n'
        assert m_ in src
        # cable.c nao chama srand(): sem isto todas as instancias repetem a mesma sequencia de erros
        src = src.replace(m_, m_ + '    { struct timespec ts_; clock_gettime(CLOCK_REALTIME, &ts_); srand((unsigned)(ts_.tv_nsec ^ getpid())); }\n')
        d = os.path.join(BUILD, f'cable{w}')
        os.makedirs(d, exist_ok=True)
        c = os.path.join(d, 'cable.c')
        open(c, 'w').write(src)
        self.exe = os.path.join(d, 'cable')
        subprocess.check_call(['gcc', '-w', '-o', self.exe, c])
        self.fifo = os.path.join(d, 'ctl')
        if os.path.exists(self.fifo):
            os.unlink(self.fifo)
        os.mkfifo(self.fifo)
        self.ctl = os.open(self.fifo, os.O_RDWR)
        self.log = open(os.path.join(d, 'cable.log'), 'w')
        env = dict(os.environ, PATH=SOCAT + ':' + os.environ['PATH'])
        self.p = subprocess.Popen([self.exe], stdin=self.ctl, stdout=self.log, stderr=subprocess.DEVNULL, env=env, cwd=d)
        time.sleep(3)
    def cmd(self, s):
        os.write(self.ctl, (s + '\n').encode())
        time.sleep(0.15)
    def alive(self):
        return self.p.poll() is None and os.path.exists(self.tx) and os.path.exists(self.rx)
    def close(self):
        self.p.kill(); self.p.wait()
        subprocess.call(['pkill', '-f', f'link=/tmp/rcw{self.w}'])
        for f in (self.tx, self.rx, f'/tmp/rcw{self.w}eTx', f'/tmp/rcw{self.w}eRx'):
            if os.path.lexists(f):
                os.unlink(f)

def grab(pat, txt, default=''):
    m = re.search(pat, txt)
    return m.group(1) if m else default

def run_one(w, cable, t):
    exe = build_main(t['chunk'], t['timeout'])
    cable.cmd(f"baud {t['baud']}"); cable.cmd(f"prop {t['prop_us']}"); cable.cmd(f"ber {t['ber']:.8f}")
    d = os.path.join(BUILD, f'run{w}'); os.makedirs(d, exist_ok=True)
    out = os.path.join(d, 'rx.bin')
    if os.path.exists(out): os.unlink(out)
    fr, ft = open(os.path.join(d, 'rx.log'), 'w'), open(os.path.join(d, 'tx.log'), 'w')
    rx = subprocess.Popen([exe, cable.rx, str(t['baud']), 'rx', out], stdout=fr, stderr=subprocess.STDOUT)
    time.sleep(0.3)
    tx = subprocess.Popen([exe, cable.tx, str(t['baud']), 'tx', FILE], stdout=ft, stderr=subprocess.STDOUT, cwd=PROJ)
    status = 'ok'
    try:
        tx.wait(timeout=t.get('limit', 900))
    except subprocess.TimeoutExpired:
        tx.kill(); tx.wait(); status = 'tx_timeout'
    try:
        rx.wait(timeout=25)
    except subprocess.TimeoutExpired:
        rx.send_signal(signal.SIGKILL); rx.wait()
        if status == 'ok': status = 'rx_killed'
    fr.close(); ft.close()
    tl, rl = open(ft.name, errors='replace').read(), open(fr.name, errors='replace').read()
    el = grab(r'ELAPSED ([\d.]+)', tl)
    h = hashlib.md5(open(out, 'rb').read()).hexdigest() if os.path.exists(out) else ''
    r = dict(t)
    r.update(status=status, elapsed=el, intact=int(h == FHASH), rcv_size=os.path.getsize(out) if h else 0,
             i_sent=grab(r'Tramas I enviadas:\s+(\d+)', tl), retrans=grab(r'Retransmiss\S+:\s+(\d+)', tl),
             timeouts=grab(r'Timeouts:\s+(\d+)', tl), rr_rcv=grab(r'RR recebidos:\s+(\d+)', tl),
             rej_rcv=grab(r'REJ recebidos:\s+(\d+)', tl), i_acc=grab(r'Tramas I aceites:\s+(\d+)', rl),
             dup=grab(r'Duplicados:\s+(\d+)', rl), rej_sent=grab(r'REJ enviados:\s+(\d+)', rl),
             failed=int('Falhou' in tl or not el), worker=w)
    return r

FIELDS = ['exp','baud','prop_us','ber','chunk','timeout','rep','status','elapsed','intact','rcv_size','i_sent','retrans','timeouts','rr_rcv','rej_rcv','i_acc','dup','rej_sent','failed','worker']

def plan(name):
    T = []
    def add(exp, reps, baud=9600, prop_us=0, ber=0.0, chunk=997, timeout=4):
        for r in range(reps):
            T.append(dict(exp=exp, baud=baud, prop_us=prop_us, ber=ber, chunk=chunk, timeout=timeout, rep=r))
    if name == 'smoke':
        add('smoke', 2); add('smoke', 2, ber=1e-4); add('smoke', 1, prop_us=200000); add('smoke', 1, baud=115200); add('smoke', 1, chunk=97)
        return T
    for b in (0, 1e-5, 2e-5, 5e-5, 1e-4, 1.5e-4, 2e-4, 3e-4):
        add('fer', 10 if b else 3, ber=b)
    for p in (0, 10000, 50000, 100000, 200000, 500000, 1000000):
        add('prop9600', 3, prop_us=p)
        add('prop115200', 3, baud=115200, prop_us=p)
    for c in (1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200):
        add('baud0', 3, baud=c)
        add('baud100ms', 3, baud=c, prop_us=100000)
    for c in (1200, 2400):
        add('baudT30', 3, baud=c, timeout=30)
    for ch in (47, 97, 197, 397, 697, 997):
        add('L0', 3, chunk=ch)
        add('Lber', 10, chunk=ch, ber=1e-4)
        add('Lprop', 3, chunk=ch, prop_us=100000)
    return T

def main():
    outp, nw, pl = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    os.makedirs(BUILD, exist_ok=True)
    tasks = plan(pl)
    random.seed(20261105); random.shuffle(tasks)
    q = queue.Queue()
    for t in tasks: q.put(t)
    total = len(tasks); done = [0]
    f = open(outp, 'w', newline=''); wr = csv.DictWriter(f, FIELDS); wr.writeheader(); f.flush()
    def work(w):
        cable = Cable(w)
        while True:
            try: t = q.get_nowait()
            except queue.Empty: break
            if not cable.alive():
                print('RESTART cable', w, flush=True)
                cable.close(); cable = Cable(w)
            r = run_one(w, cable, t)
            with lock:
                wr.writerow(r); f.flush(); done[0] += 1
                print(done[0], '/', total, t['exp'], t['baud'], t['prop_us'], t['ber'], t['chunk'], t['timeout'], '->', r['status'], r['elapsed'], 'intact', r['intact'], 'to', r['timeouts'], 'rej', r['rej_rcv'], flush=True)
        cable.cmd('ber 0'); cable.close()
    th = [threading.Thread(target=work, args=(i,)) for i in range(nw)]
    for x in th: x.start()
    for x in th: x.join()
main()
