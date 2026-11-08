import importlib.util as u, sys
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import cm
from reportlab.lib.styles import ParagraphStyle as PS
from reportlab.lib.enums import TA_JUSTIFY, TA_CENTER
from reportlab.lib import colors
from reportlab.platypus import SimpleDocTemplate, Paragraph as P0, Spacer, Image, Table, TableStyle, KeepTogether
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
def P(t,st):
    t=t.replace('<sub>','<sub rise="2.2" size="8">').replace('<super>','<super rise="4" size="8">')
    return P0(t,st)
sp=u.spec_from_file_location('an','analysis.py'); a=u.module_from_spec(sp); sp.loader.exec_module(a)
TURMA = sys.argv[2] if len(sys.argv)>2 else '[A PREENCHER]'
D='/usr/share/fonts/truetype/liberation/LiberationSerif-'
for n,f in (('LS','Regular'),('LS-B','Bold'),('LS-I','Italic'),('LS-BI','BoldItalic')): pdfmetrics.registerFont(TTFont(n,D+f+'.ttf'))
pdfmetrics.registerFontFamily('LS',normal='LS',bold='LS-B',italic='LS-I',boldItalic='LS-BI')
INK=colors.HexColor('#16365c')
B=PS('b',fontName='LS',fontSize=11,leading=12.7,alignment=TA_JUSTIFY,spaceAfter=3.5)
H=PS('h',fontName='LS-B',fontSize=11,leading=13,textColor=INK,spaceBefore=4,spaceAfter=2,keepWithNext=1)
T=PS('t',fontName='LS-B',fontSize=15,leading=18,alignment=TA_CENTER,textColor=INK,spaceAfter=3)
AU=PS('a',fontName='LS',fontSize=11,leading=13,alignment=TA_CENTER)
CAP=PS('c',fontName='LS-I',fontSize=11,leading=12.5,alignment=TA_JUSTIFY,spaceBefore=2,spaceAfter=5)
EQ=PS('e',fontName='LS',fontSize=11,leading=14,alignment=TA_CENTER,spaceAfter=3.5)
TC=PS('tc',fontName='LS',fontSize=11,leading=12,alignment=TA_CENTER)
Tp='T<sub>prop</sub>'; Tf='T<sub>f</sub>'; To='T<sub>out</sub>'
s=[]
s+=[P('Caracterização da Eficiência de um Protocolo de Ligação de Dados <i>Stop-and-Wait</i>',T),
    P(f'Leonor Alpoim (up202407742) e André Baptista (up202403287) · Turma {TURMA}',AU),
    P('Redes de Computadores, L.EIC, FEUP · Lab 1 · 2026/2027',AU),Spacer(1,4)]
s+=[P('Sumário',H),P(f'Este relatório avalia a eficiência (S) do protocolo <i>Stop-and-Wait</i> implementado no Lab 1, com base em 265 transferências de um ficheiro de 10 968 bytes sobre um cabo virtual, variando o FER, {Tp}, a capacidade C e o tamanho de trama L. Sem erros, S atinge 0,77, limitada pela codificação 8N1 e pelo <i>overhead</i> do protocolo. A fórmula clássica reproduz as tendências, mas sobrestima S entre 30 % e 71 % nos ensaios sem erros; um modelo refinado reproduz as mesmas medições com desvio máximo de 0,17 %. Os ensaios identificaram dois limites da implementação: o {To} de 4 s impede a transferência com C ≤ 2400 bit/s, e o BCC2 deixou passar erros em 39 de 130 transferências com ruído.',B)]
s+=[P('Introdução',H),
 P('O protocolo implementado transfere ficheiros de forma fiável entre dois computadores Linux por uma ligação série RS-232. A ligação é estabelecida com SET/UA e os dados seguem em tramas I delimitadas por FLAG, com <i>byte stuffing</i>, proteção por BCC1 (cabeçalho) e BCC2 (XOR dos dados). O recetor responde com RR ou REJ, e o emissor retransmite ao receber REJ ou quando expira o temporizador (4 s, desistindo ao fim de 4 tentativas sem resposta).',B),
 P(f'A eficiência é a razão entre o débito útil e a capacidade, S = R/C. Para <i>Stop-and-Wait</i> com taxa de erros de trama FER, o modelo clássico é:',B),
 P(f'S<sub>clássica</sub> = (1 − FER) / (1 + 2a),   com a = {Tp} / {Tf}   e   {Tf} = 10·L<sub>f</sub> / C',EQ),
 P(f'L<sub>f</sub> é o comprimento da trama no cabo e cada byte ocupa 10 tempos de bit (8N1). Neste trabalho, L<sub>f</sub> = 1011 bytes é a mediana das tramas de dados, o que dá {Tf} = 1,054 s a 9600 bit/s.',B)]
s+=[P('Metodologia',H),
 P(f'As medições usaram o programa cable.c, que emula a ligação com C, taxa de erros de bit (BER) e {Tp} configuráveis, sobre portas série virtuais (socat). Os resultados caracterizam portanto o protocolo num cabo emulado, e não em <i>hardware</i> RS-232. O erro é introduzido nos dois sentidos, afetando também as tramas RR e REJ. A configuração de referência foi C = 9600 bit/s, {Tp} = 0, BER = 0, L = 1000 bytes (campo de dados da trama I) e {To} = 4 s, variando um parâmetro de cada vez:',B),
 P(f'<b>FER:</b> BER de 0 a 3×10<super>−4</super> (8 valores). <b>{Tp}:</b> 0 a 1 s (7 valores), a 9600 e a 115 200 bit/s. <b>C:</b> 1200 a 115 200 bit/s (8 valores), com {Tp} de 0 e de 100 ms. <b>L:</b> 50 a 1000 bytes (6 valores), na referência, com {Tp} = 100 ms e com BER = 10<super>−4</super>. Repetiram-se 3 vezes os ensaios sem ruído, por serem determinísticos, e 10 vezes os ensaios com ruído.',B),
 P(f'O tempo T foi medido no emissor, entre o início do llOpen e a confirmação do pacote END, excluindo o fecho da ligação. Daí resultam R = 8·D/T, com D = 10 968 bytes, e S = R/C. O FER medido é 1 − (tramas I aceites / tramas I enviadas). A integridade foi verificada comparando o <i>hash</i> MD5 dos ficheiros. Os pacotes de controlo tinham 76 bytes, por incluírem o caminho completo do ficheiro. Os ensaios usaram cópias do código, alteradas apenas no cronómetro, em CHUNK_SIZE e, para {To} = 30 s, em TIMEOUT. Na cópia do cable.c, alteraram-se os caminhos das portas (para correr até 8 instâncias em paralelo) e acrescentou-se uma semente a rand(), pois sem ela as repetições geravam a mesma sequência de erros. As 11 corridas realizadas logo após uma transferência falhada foram repetidas, porque o cabo ainda continha bytes residuais.',B),
 P(f'O modelo refinado usa os comprimentos reais de todas as tramas e não recorre a valores medidos. Cada trama i é transmitida em média 1/P<sub>i</sub> vezes e cada tentativa dura um ciclo normal ou, com probabilidade h, o tempo {To}:',B),
 P(f'T = T<sub>lig</sub> + Σ<sub>i</sub> [(1−h)·({Tf}<sub>,i</sub> + T<sub>ack</sub> + 2{Tp}) + h·{To}] / P<sub>i</sub>,   S = 8·D / (C·T)',EQ),
 P(f'Aqui, T<sub>lig</sub> = 100/C + 2{Tp} (SET/UA) e T<sub>ack</sub> = 50/C. A probabilidade h = 1 − (1 − BER)<super>80</super> corresponde a erro nos 10 bytes de cabeçalho, FLAG final e RR, que provoca <i>timeout</i>. A probabilidade de aceitação P<sub>i</sub> = (1−h)·[(1 + exp(−2·BER·(L<sub>f,i</sub> − 5)))/2]<super>8</super> considera que o XOR só deteta erros quando alguma das 8 colunas de bits tem paridade ímpar.',B)]
s+=[P('Resultados',H),Image('fig.png',width=17*cm,height=17*cm*11.6/17),
 P(f'Figura 1 – Eficiência medida (marcadores) e prevista (linhas) em função de (a) FER, (b) a, (c) C e (d) L. As barras de erro representam ±1σ. Em (c), as cruzes assinalam as transferências falhadas.',CAP)]
s+=[P(f'<b>Sem erros nem atraso, S mantém-se entre 0,7699 e 0,7705 para C entre 4800 e 115 200 bit/s (Fig. 1c).</b> O valor previsto, 0,7705, decompõe-se no fator 8/10 da codificação 8N1 e no fator 0,963 do protocolo, que usa 11 388 bytes no cabo para entregar 10 968. A 9600 bit/s, a transferência demora T = 11,862 s, com σ inferior a 1 ms.',B),
 P(f'<b>O atraso de propagação reduz S de acordo com a forma 1/(1+2a) (Fig. 1b).</b> A 9600 bit/s mediu-se T = 11,862 + 30·{Tp} s, o que corresponde a 15 idas-e-voltas: SET/UA e 14 tramas I. Com a = 0,95, S desce para 0,218 e, com a = 11,4, para 0,025. Os pontos das duas capacidades caem na mesma curva: C = 4800 bit/s com {Tp} = 100 ms e C = 9600 bit/s com {Tp} = 50 ms dão ambos S = 0,6840. Estes resultados confirmam que C e {Tp} influenciam S através de a. O modelo refinado desvia-se no máximo 0,17 %. A fórmula clássica sobrestima S em 30 % com a = 0 e em 71 % com a = 11,4, porque ignora o <i>overhead</i> e o facto de as 14 tramas transportarem apenas 11 pacotes completos: o último pacote de dados leva 1 byte e dois são pacotes de controlo, mas cada um custa uma ida-e-volta.',B)]
rows=[['BER','FER<br/>med.','FER<br/>mod.','T (s)','R<br/>(bit/s)','S<br/>med.','S<br/>mod.','1 − FER','N<sub>to</sub>','Corromp.']]
fmt=lambda x,n=3: f'{x:.{n}f}'.replace('.',',')
lab={0:'0',1e-5:'1×10<super>−5</super>',2e-5:'2×10<super>−5</super>',5e-5:'5×10<super>−5</super>',1e-4:'1×10<super>−4</super>',1.5e-4:'1,5×10<super>−4</super>',2e-4:'2×10<super>−4</super>',3e-4:'3×10<super>−4</super>'}
for k,v in a.grp('fer','ber').items():
    q=a.summ(v); p=a.predict(9600,0,k,997)
    rows.append([lab[k],fmt(q['fer']),fmt(p[2]),fmt(q['T'],1),f"{q['R']:.0f}",fmt(q['S']),fmt(p[0]),fmt(1-q['fer']),fmt(q['to'],1),f"{q['corrupt']}/{q['nok']}"])
tb=Table([[P(str(c),TC) for c in r] for r in rows],colWidths=[1.95*cm,1.45*cm,1.45*cm,1.35*cm,1.6*cm,1.45*cm,1.45*cm,1.75*cm,1.2*cm,1.9*cm],repeatRows=1)
tb.setStyle(TableStyle([('LINEABOVE',(0,0),(-1,0),.8,INK),('LINEBELOW',(0,0),(-1,0),.5,INK),('LINEBELOW',(0,-1),(-1,-1),.8,INK),('VALIGN',(0,0),(-1,-1),'MIDDLE'),('TOPPADDING',(0,0),(-1,-1),.6),('BOTTOMPADDING',(0,0),(-1,-1),1.6),('LEFTPADDING',(0,0),(-1,-1),1),('RIGHTPADDING',(0,0),(-1,-1),1)]))
s+=[P(f'<b>A eficiência decresce quase linearmente com o FER, mas abaixo de 1 − FER (Fig. 1a, Tabela 1).</b> Os valores medidos desta série ficam a menos de 1σ do modelo refinado. Com BER = 3×10<super>−4</super>, S = 0,092, face a 0,157 pela fórmula clássica, e os 3,2 <i>timeouts</i> por transferência custam 12,8 s dos 104,7 s totais. Cada REJ custa uma única retransmissão, enquanto cada <i>timeout</i> custa 4 s, cerca de 3,8 vezes {Tf}.',B),
 KeepTogether([tb,P('Tabela 1 – Efeito do BER com L = 1000 bytes e C = 9600 bit/s (10 repetições; 3 para BER = 0). N<sub>to</sub>: média de <i>timeouts</i> por transferência. Corromp.: ficheiros recebidos com erros.',CAP)]),
 P(f'<b>O tamanho ótimo de trama depende das condições da ligação (Fig. 1d).</b> Na referência, S sobe de 0,605 (L = 50) para 0,770 (L = 1000) porque o <i>overhead</i> por trama pesa menos. Com {Tp} = 100 ms, o aumento é maior, de 0,146 para 0,615, pois há menos idas-e-voltas. Com BER = 10<super>−4</super>, a tendência inverte-se a partir de L ≈ 400: S máximo = 0,573 para L entre 200 e 400, contra 0,316 com L = 1000 e 0,396 com L = 50. Com L = 50, os 2,2 <i>timeouts</i> ocupam 8,8 s dos 24,5 s totais; sem eles, o modelo preveria S = 0,576, o que mostra que os <i>timeouts</i> dominam em tramas pequenas.',B),
 P(f'<b>Com {To} = 4 s, todas as transferências a 1200 e 2400 bit/s falharam (12 em 12, Fig. 1c).</b> A maior trama demora {Tf} = 4,25 s a 2400 bit/s, mais do que {To}. O registo do emissor mostra <i>timeouts</i> prematuros, duplicados em fila e desistência após 4 tentativas. Com {To} = 30 s, ambas as capacidades atingem S = 0,7705, sem qualquer retransmissão.',B),
 P('<b>O BCC2 não detetou todos os erros: 39 de 130 transferências concluídas com ruído entregaram um ficheiro corrompido</b>, com o tamanho correto (Tabela 1). Em 3 ficheiros inspecionados, o XOR das diferenças é 00 em todos os pacotes afetados, ou seja, os erros anulam-se no BCC2. O efeito cresce com o BER e com L: com BER = 10<super>−4</super>, surgiu em 0 de 30 transferências com L ≤ 200 e em 4 de 10 com L = 1000. Os valores de R e S incluem estes bits errados, pelo que são um limite superior do débito correto.',B)]
s+=[P('Conclusões',H),
 P(f'A forma S = (1 − FER)/(1 + 2a) descreve corretamente as tendências observadas, mas serve como limite superior: o protocolo atingiu no máximo S = 0,77. Incluindo a codificação 8N1, os comprimentos reais das tramas, as confirmações e os <i>timeouts</i>, o modelo refinado desvia-se no máximo 0,17 % sem ruído e fica a cerca de 1σ ou menos das medições com ruído.',B),
 P(f'As medições sustentam duas medidas: usar L entre 200 e 400 bytes em ligações com BER próximo de 10<super>−4</super>, onde S quase duplica face a L = 1000; e garantir {To} superior a {Tf} + T<sub>ack</sub> + 2{Tp}, o que tornou viáveis as transferências a 1200 e 2400 bit/s. Para garantir a integridade do ficheiro em ligações ruidosas, o passo seguinte é substituir o BCC2 por um código mais forte, como um CRC-16, e medir o seu efeito.',B)]
doc=SimpleDocTemplate(sys.argv[1],pagesize=A4,leftMargin=2*cm,rightMargin=2*cm,topMargin=1.6*cm,bottomMargin=1.6*cm,title='Caracterização da Eficiência de um Protocolo de Ligação de Dados Stop-and-Wait',author='Leonor Alpoim, André Baptista',subject='Redes de Computadores - Lab 1')
def foot(c,d):
    c.setFont('LS',11); c.drawCentredString(A4[0]/2,.85*cm,str(d.page))
doc.build(s,onFirstPage=foot,onLaterPages=foot)
