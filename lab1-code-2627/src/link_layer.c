// RCOM 2026/2027
//
// Link layer protocol implementation
//
// PAPEL DESTE FICHEIRO
// A camada de ligação garante que cada pacote da aplicação chega certo ao outro
// lado, por cima de uma porta série que só transporta bytes e pode perdê-los ou
// corrompê-los. Usa Stop-and-Wait: envia uma trama e espera pela confirmação
// antes de enviar a seguinte. O conteúdo dos pacotes não lhe interessa.
//
// ÍNDICE (usa Ctrl+F com a etiqueta)
//   #CONSTANTES     valores de FLAG, A, C e ESC
//   #Ns_Nr          números de sequência do emissor e do recetor
//   #ESTATISTICAS   contadores impressos no llClose
//   #ESTADOS        máquina de estados que lê as tramas
//   #BCC1           verificação do cabeçalho
//   #BCC2           verificação dos dados
//   #STUFFING       byte stuffing no emissor
//   #DESTUFFING     byte destuffing no recetor
//   #TIMEOUT        alarme e deteção de tempo esgotado
//   #RETRANSMISSAO  ciclos que voltam a enviar uma trama
//   #SET_UA         estabelecimento da ligação
//   #RR             confirmações positivas
//   #REJ            confirmações negativas
//   #DUPLICADO      deteção de tramas repetidas
//   #DISC           terminação da ligação
//
// FORMATO DAS TRAMAS
//   Supervisão: FLAG  A  C  BCC1  FLAG                 (5 bytes)
//   Informação: FLAG  A  C  BCC1  dados  BCC2  FLAG    (dados e BCC2 com stuffing)

#include "link_layer.h"    // Struct LinkLayer e declaração das funções ll*
#include "serial_port.h"   // openSerialPort, readByteSerialPort, writeBytesSerialPort

#include <signal.h>        // sigaction e SIGALRM, usados no timeout
#include <stdio.h>         // printf e perror
#include <stdlib.h>        // exit
#include <unistd.h>        // alarm e sleep
#include <string.h>        // memcpy

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source
#define BUF_SIZE 256    // Tamanho de buffer genérico (não é usado neste ficheiro)

#define FALSE 0         // Valor lógico falso
#define TRUE 1          // Valor lógico verdadeiro

// #CONSTANTES
#define FLAG  0x7E      // Delimita o início e o fim de cada trama
#define A_TX  0x03      // Campo A: comandos do emissor e respostas do recetor
#define C_SET 0x03      // Campo C do SET: o emissor pede para abrir a ligação
#define C_UA  0x07      // Campo C do UA: confirma um SET ou um DISC
#define A_RX   0x01     // Campo A: comandos do recetor e respostas do emissor
#define C_DISC 0x0B     // Campo C do DISC: pedido para terminar a ligação

#define ESC   0x7D      // Byte de escape do byte stuffing
// Campos C que dependem do número de sequência n (0 ou 1)
#define C_I(n)   ((unsigned char)((n) << 7))   // I0 = 0x00, I1 = 0x80
#define C_RR(n)  ((unsigned char)(0xAA | (n))) // RR0 = 0xAA, RR1 = 0xAB
#define C_REJ(n) ((unsigned char)(0x54 | (n))) // REJ0 = 0x54, REJ1 = 0x55

#define MAX_PAYLOAD 1000   // Máximo de bytes de dados por trama
// Pior caso: 4 de cabeçalho + (dados + BCC2) todos duplicados pelo stuffing + FLAG final
#define MAX_FRAME (4 + 2 * (MAX_PAYLOAD + 1) + 1)

// #TIMEOUT: TRUE enquanto o alarme está a contar; o handler põe-no a FALSE
int alarmEnabled = FALSE;

static int timeoutSec = 3;          // Timeout em segundos, substituído no llOpen
static int maxRetransmissions = 3;  // Retransmissões permitidas, substituídas no llOpen
// #Ns_Nr: cada um só vale 0 ou 1 e alterna com ^= 1 (Stop-and-Wait precisa de 1 bit)
static unsigned char Ns = 0;   // número da próxima trama I a enviar
static unsigned char Nr = 0;   // número da trama I que o recetor espera

// #TIMEOUT: alarmes disparados na operação atual; decide quando se desiste
int alarmCount = 0;

// #ESTATISTICAS
// Estatísticas do protocolo, impressas no llClose
static struct
{
    int iSent;            // tramas I enviadas (contando retransmissões)
    int retransmissions;  // tramas (SET, I, DISC) reenviadas
    int timeouts;         // alarmes disparados
    int rrReceived;       // RR recebidos pelo emissor
    int rejReceived;      // REJ recebidos pelo emissor
    int iReceived;        // tramas I novas aceites
    int duplicates;       // tramas I duplicadas descartadas
    int rrSent;           // RR enviados pelo recetor
    int rejSent;          // REJ enviados pelo recetor
} stats;

// #ESTADOS: estados das máquinas de leitura. DATA e DATA_ESC só existem em llReceive
typedef enum { START, FLAG_RCV, A_RCV, C_RCV, BCC_OK, STOP, DATA, DATA_ESC } State;

// #ESTADOS #BCC1
// Lê uma trama [FLAG, A, C, A^C, FLAG] com endereço a e devolve o C recebido.
// Se for o cabeçalho válido de uma trama I, devolve logo o C sem ler os dados.
// Devolve -1 se o alarme disparou (só com useTimeout)
static int readSupervision(unsigned char a, int useTimeout)
{
    State state = START;           // Começa à procura da FLAG inicial
    unsigned char byte;            // Último byte lido
    unsigned char cRecebido = 0;   // Campo C da trama que está a ser lida

    while (state != STOP)          // Repete até ter uma trama completa
    {
        // #TIMEOUT: o handler do alarme pôs alarmEnabled a FALSE, logo o tempo acabou
        if (useTimeout && !alarmEnabled)
            return -1;

        // O read bloqueia até chegar um byte. O SIGALRM interrompe-o e devolve -1,
        // e o continue volta à verificação do alarme acima.
        if (readByteSerialPort(&byte) != 1)
            continue;

        switch (state)             // A reação depende do estado atual
        {
        case START:                // À procura do início da trama
            if (byte == FLAG) state = FLAG_RCV;   // Encontrou a FLAG inicial
            break;                                // Outro byte qualquer é ignorado
        case FLAG_RCV:             // Já viu a FLAG, espera o campo A
            if (byte == a) state = A_RCV;         // Endereço certo
            else if (byte != FLAG) state = START; // Outro byte: não é para nós. FLAG repetida: fica aqui
            break;
        case A_RCV:                // Já viu A, espera o campo C
            if (byte == FLAG) state = FLAG_RCV;   // FLAG: pode ser o início de nova trama
            else
            {
                cRecebido = byte;  // Guarda o C para verificar o BCC1
                state = C_RCV;     // Segue para o BCC1
            }
            break;
        case C_RCV:                // Já viu C, espera o BCC1
            if (byte == (a ^ cRecebido))  // #BCC1: tem de ser igual a A xor C
            {
                // Numa trama I há dados depois do BCC1: devolve já e o resto é
                // ignorado depois, no estado START, até à próxima FLAG
                if (cRecebido == C_I(0) || cRecebido == C_I(1))
                    return cRecebido;
                state = BCC_OK;    // Trama de supervisão: falta a FLAG final
            }
            else if (byte == FLAG) state = FLAG_RCV;  // FLAG: pode ser o início de nova trama
            else state = START;   // BCC1 errado: ignora a trama
            break;
        case BCC_OK:               // Cabeçalho certo, espera a FLAG final
            if (byte == FLAG) state = STOP;  // Trama completa: sai do while
            else state = START;              // Faltou a FLAG: descarta
            break;
        default:                   // DATA e DATA_ESC não são usados aqui
            break;
        }
    }

    return cRecebido;              // Devolve o C da trama lida
}

// Espera por uma trama com A = a e C = c (ignora as outras)
// Devolve 0 se recebeu a trama, -1 se o alarme disparou (só com useTimeout)
int receiveFrame(unsigned char a, unsigned char c, int useTimeout)
{
    while (TRUE)                   // Repete até vir a trama certa ou o alarme
    {
        int resp = readSupervision(a, useTimeout);  // Lê a trama seguinte
        if (resp == -1) return -1;                  // Alarme disparou
        if (resp == c) return 0;                    // Era a trama pretendida
    }
}

// #RR #REJ
// Espera por um RR ou REJ. Devolve o C recebido ou -1 se o alarme disparou
int receiveSupervision(unsigned char a)
{
    while (TRUE)                   // Ignora tudo o resto, por exemplo um UA repetido
    {
        int resp = readSupervision(a, TRUE);  // Lê sempre com timeout
        if (resp == -1 || resp == C_RR(0) || resp == C_RR(1) ||
            resp == C_REJ(0) || resp == C_REJ(1))
            return resp;           // Alarme, RR ou REJ: entrega a quem chamou
    }
}

// Monta e envia uma trama de supervisão de 5 bytes com A = a e C = c
static void sendSupervision(unsigned char a, unsigned char c)
{
    unsigned char frame[5] = {FLAG, a, c, a ^ c, FLAG};  // a ^ c é o BCC1
    writeBytesSerialPort(frame, 5);                      // Envia os 5 bytes
}

// #STUFFING
// Escreve o byte b em out, já com stuffing, e devolve quantos bytes escreveu.
// 0x7E passa a 0x7D 0x5E e 0x7D passa a 0x7D 0x5D, para a FLAG nunca aparecer
// dentro da trama.
static int stuffByte(unsigned char b, unsigned char *out)
{
    if (b == FLAG || b == ESC)     // Bytes que não podem seguir tal e qual
    {
        out[0] = ESC;              // Primeiro o byte de escape
        out[1] = b ^ 0x20;         // Depois o byte original com xor 0x20
        return 2;                  // Este byte passou a ocupar 2
    }
    out[0] = b;                    // Byte normal: copia-se
    return 1;                      // Ocupa 1 byte
}

// #TIMEOUT
// Corre quando chega o SIGALRM, ou seja, quando o tempo do alarm() acabou
void alarmHandler(int signal)
{
    alarmEnabled = FALSE;          // Avisa o ciclo de leitura de que o tempo acabou
    alarmCount++;                  // Conta mais uma tentativa falhada
    stats.timeouts++;              // Atualiza as estatísticas

    printf("Alarm #%d received\n", alarmCount);  // Mostra o alarme na consola
}

// #TIMEOUT
// Associa o SIGALRM ao alarmHandler. sa_flags fica a 0, sem SA_RESTART: é isso
// que faz o read bloqueante ser interrompido pelo sinal em vez de recomeçar.
static void installAlarm()
{
    struct sigaction act = {0};         // Tudo a zero, incluindo sa_flags
    act.sa_handler = &alarmHandler;     // Função a correr quando o sinal chegar
    if (sigaction(SIGALRM, &act, NULL) == -1)  // Regista o handler
    {
        perror("sigaction");            // Mostra o erro do sistema
        exit(1);                        // Sem alarme não há timeout: termina
    }
}

// #ESTATISTICAS
// Imprime os contadores relevantes para o papel "tx" ou "rx"
static void printStats(const char *role)
{
    printf("\n--- Estatísticas (%s) ---\n", role);  // Cabeçalho
    if (role[0] == 't')            // Emissor
    {
        printf("Tramas I enviadas:   %d\n", stats.iSent);
        printf("Retransmissões:      %d\n", stats.retransmissions);
        printf("Timeouts:            %d\n", stats.timeouts);
        printf("RR recebidos:        %d\n", stats.rrReceived);
        printf("REJ recebidos:       %d\n", stats.rejReceived);
    }
    else                           // Recetor
    {
        printf("Tramas I aceites:    %d\n", stats.iReceived);
        printf("Duplicados:          %d\n", stats.duplicates);
        printf("RR enviados:         %d\n", stats.rrSent);
        printf("REJ enviados:        %d\n", stats.rejSent);
        printf("Retransmissões:      %d\n", stats.retransmissions);
        printf("Timeouts:            %d\n", stats.timeouts);
    }
    printf("-------------------------\n");  // Linha final
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
// #SET_UA #RETRANSMISSAO
// Emissor: abre a porta e envia SET até receber UA. Devolve 0 ou -1.
int llOpenTx(LinkLayer llParameters)
{
    timeoutSec = llParameters.timeout;                  // Guardado para llSend e llCloseTx
    maxRetransmissions = llParameters.nRetransmissions; // Guardado para llSend e llCloseTx

    if (openSerialPort(llParameters.serialPort, llParameters.baudRate) < 0)  // Abre e configura a porta
    {
        perror("openSerialPort");  // Mostra o erro
        return -1;                 // Sem porta não há ligação
    }

    printf("Serial port %s opened\n", llParameters.serialPort);

    installAlarm();                // Prepara o SIGALRM para os timeouts

    unsigned char set[5] = {FLAG, A_TX, C_SET, A_TX ^ C_SET, FLAG};  // A trama SET
    alarmCount = 0;                // Ainda não houve alarmes

    // Com nRetransmissions = 3 corre para 0, 1, 2 e 3 alarmes: 4 envios no máximo
    while (alarmCount <= llParameters.nRetransmissions)
    {
        // Enviar SET
        if (alarmCount > 0) stats.retransmissions++;  // A partir do 2.º envio é retransmissão
        writeBytesSerialPort(set, 5);                 // Envia os 5 bytes
        printf("SET enviado (tentativa %d)\n", alarmCount + 1);

        alarm(llParameters.timeout); // Set alarm to be triggered after timeout
        alarmEnabled = TRUE;         // O ciclo de leitura passa a respeitar o alarme

        // Receber UA
        if (receiveFrame(A_TX, C_UA, TRUE) == 0)      // 0 = chegou o UA; -1 = alarme
        {
            alarm(0); // Cancel alarm
            alarmEnabled = FALSE;    // Deixa de haver alarme ativo
            printf("UA recebido. Ligação estabelecida\n");
            return 0;                // Ligação aberta com sucesso
        }
    }

    printf("Sem resposta após %d tentativas\n", alarmCount);  // Esgotou as tentativas

    // Close serial port
    if (closeSerialPort() < 0)
    {
        perror("closeSerialPort");
        return -1;
    }

    printf("Serial port %s closed\n", llParameters.serialPort);

    return -1;                     // A ligação não foi estabelecida
}

// #SET_UA
// Recetor: abre a porta, espera pelo SET e responde com UA. Devolve 0 ou -1.
int llOpenRx(LinkLayer llParameters)
{
    timeoutSec = llParameters.timeout;                  // Guardado para llCloseRx
    maxRetransmissions = llParameters.nRetransmissions; // Guardado para llCloseRx

    if (openSerialPort(llParameters.serialPort, llParameters.baudRate) < 0)  // Abre e configura a porta
    {
        perror("openSerialPort");  // Mostra o erro
        return -1;                 // Sem porta não há ligação
    }

    printf("Serial port %s opened\n", llParameters.serialPort);

    installAlarm();   // usado no llCloseRx para retransmitir o DISC

    // Receber SET (FALSE = sem timeout: o recetor espera o tempo que for preciso)
    receiveFrame(A_TX, C_SET, FALSE);
    printf("SET recebido\n");

    // Responder com UA. Se se perder, o llReceive trata do SET repetido
    sendSupervision(A_TX, C_UA);
    printf("UA enviado. Ligação estabelecida\n");

    // nao fechei o port para o caso de enviar e receber dados depois

    return 0;                      // Ligação aberta
}

////////////////////////////////////////////////
// LLSEND
////////////////////////////////////////////////
// #STUFFING #BCC2 #RETRANSMISSAO #RR #REJ
// Envia um pacote numa trama I e só volta depois de ser confirmado (Stop-and-Wait).
// Devolve o número de bytes da trama na linha, ou -1 se desistir.
int llSend(const unsigned char *buf, int bufSize)
{
    if (buf == NULL || bufSize <= 0 || bufSize > MAX_PAYLOAD)  // Valida os argumentos
        return -1;

    unsigned char frame[MAX_FRAME];  // Trama completa, a mesma em cada retransmissão
    int len = 0;                     // Tamanho da trama até agora
    unsigned char c = C_I(Ns);       // C = I0 ou I1, conforme Ns

    // Cabeçalho
    frame[len++] = FLAG;             // FLAG inicial
    frame[len++] = A_TX;             // Endereço
    frame[len++] = c;                // Controlo
    frame[len++] = A_TX ^ c;   // BCC1

    // Dados + BCC2
    unsigned char bcc2 = 0;          // XOR acumulado dos dados
    for (int i = 0; i < bufSize; i++)
    {
        bcc2 ^= buf[i];              // #BCC2: calculado sobre o byte original, antes do stuffing
        len += stuffByte(buf[i], &frame[len]);  // Escreve o byte com stuffing e avança len
    }
    len += stuffByte(bcc2, &frame[len]);        // O BCC2 também pode ser 7E ou 7D

    // Fim
    frame[len++] = FLAG;             // FLAG final

    alarmCount = 0;                  // Cada trama começa com os alarmes a zero
    int enviadas = 0;                // Quantas vezes esta trama foi enviada
    while (alarmCount <= maxRetransmissions)  // #RETRANSMISSAO: desiste ao 4.º alarme
    {
        if (enviadas > 0) stats.retransmissions++;  // Do 2.º envio em diante é retransmissão
        writeBytesSerialPort(frame, len);           // Envia a trama completa
        stats.iSent++;                              // Conta todas as tramas enviadas
        enviadas++;                                 // Conta os envios desta trama
        printf("I%d enviada (%d bytes, tentativa %d)\n", Ns, bufSize, enviadas);

        alarm(timeoutSec);           // #TIMEOUT: arma o alarme
        alarmEnabled = TRUE;         // A leitura passa a respeitar o alarme

        // Um RR(Ns) é um RR atrasado de uma trama anterior: continua à espera
        int resp;                    // C recebido, ou -1 se o alarme disparou
        do
        {
            resp = receiveSupervision(A_TX);  // Espera por RR, REJ ou alarme
        } while (resp == C_RR(Ns));           // Não reinicia o timer

        if (resp == -1)
            continue;      // timeout: volta ao início e reenvia

        alarm(0);                    // Chegou resposta: cancela o alarme
        alarmEnabled = FALSE;        // Deixa de haver alarme ativo

        if (resp == C_RR(Ns ^ 1))    // #RR: o recetor espera a trama seguinte, logo confirmou esta
        {
            stats.rrReceived++;      // Atualiza as estatísticas
            printf("RR%d recebido\n", Ns ^ 1);
            Ns ^= 1;                 // #Ns_Nr: a próxima trama usa o outro número
            return len;              // Sucesso
        }
        else if (resp == C_REJ(Ns))  // #REJ: dados corrompidos, reenvia sem esperar pelo timeout
        {
            stats.rejReceived++;     // Atualiza as estatísticas
            printf("REJ%d recebido, a retransmitir\n", Ns);
            continue;                // alarmCount não sobe: REJ não conta para o limite
        }
        // Um REJ(Ns ^ 1) antigo não entra em nenhum ramo: o ciclo também retransmite
    }
    printf("I%d sem confirmação após %d tentativas\n", Ns, enviadas);
    return -1;             // esgotou as tentativas
}

////////////////////////////////////////////////
// LLRECEIVE
////////////////////////////////////////////////
// #ESTADOS #DESTUFFING #BCC1 #BCC2 #DUPLICADO #RR #REJ
// Recebe uma trama I e entrega o pacote em packet. Só devolve com uma trama nova
// e correta; devolve o número de bytes do pacote. Não tem timeout: bloqueia até
// chegar uma trama válida.
int llReceive(unsigned char *packet)
{
    unsigned char data[MAX_PAYLOAD + 1];   // dados + BCC2
    unsigned char byte;                    // Último byte lido

    while (TRUE)                   // Cada volta trata uma trama recebida
    {
        State state = START;           // Começa à procura da FLAG inicial
        unsigned char cRecebido = 0;   // Campo C desta trama
        int size = 0;                  // Bytes já guardados em data

        // Ler uma trama I completa (com destuffing)
        while (state != STOP)
        {
            if (readByteSerialPort(&byte) != 1)  // Sem byte lido: tenta outra vez
                continue;

            switch (state)         // A reação depende do estado atual
            {
            case START:            // À procura do início da trama
                if (byte == FLAG) state = FLAG_RCV;   // Encontrou a FLAG inicial
                break;
            case FLAG_RCV:         // Já viu a FLAG, espera o campo A
                if (byte == A_TX) state = A_RCV;      // Endereço certo
                else if (byte != FLAG) state = START; // Outro byte: recomeça. FLAG repetida: fica aqui
                break;
            case A_RCV:            // Já viu A, espera o campo C
                if (byte == C_I(0) || byte == C_I(1) || byte == C_SET)  // Só aceita I0, I1 ou SET
                {
                    cRecebido = byte;  // Guarda para verificar o BCC1
                    state = C_RCV;     // Segue para o BCC1
                }
                else if (byte == FLAG) state = FLAG_RCV;  // Pode ser o início de nova trama
                else state = START;                       // C desconhecido: ignora
                break;
            case C_RCV:            // Já viu C, espera o BCC1
                if (byte == (A_TX ^ cRecebido))  // #BCC1: tem de ser igual a A xor C
                {
                    if (cRecebido == C_SET)      // #SET_UA: é um SET, não uma trama I
                    {
                        // O nosso UA perdeu-se e o emissor reenviou o SET
                        sendSupervision(A_TX, C_UA);
                        printf("SET repetido, UA reenviado\n");
                        state = START;           // Volta a esperar uma trama
                    }
                    else
                    {
                        size = 0;      // Começa a guardar dados do início
                        state = DATA;  // Depois do BCC1 vêm os dados
                    }
                }
                else if (byte == FLAG) state = FLAG_RCV;  // Pode ser o início de nova trama
                else state = START;   // #BCC1 errado: ignora sem resposta, o emissor recupera por timeout
                break;
            case DATA:             // A ler os dados
                if (byte == FLAG) state = STOP;           // FLAG final: trama completa
                else if (byte == ESC) state = DATA_ESC;   // #DESTUFFING: o próximo byte está escapado
                else if (size < MAX_PAYLOAD + 1) data[size++] = byte;  // Byte normal: guarda
                else state = START;   // trama demasiado grande: descarta
                break;
            case DATA_ESC:         // O byte anterior foi ESC
                if (size < MAX_PAYLOAD + 1)
                {
                    data[size++] = byte ^ 0x20;  // #DESTUFFING: repõe o byte original
                    state = DATA;                // Volta aos dados normais
                }
                else state = START;              // Trama demasiado grande: descarta
                break;
            default:               // BCC_OK e STOP não são usados aqui
                break;
            }
        }

        if (size == 0)
            continue;   // nem sequer trazia BCC2: ignora

        // Separar o BCC2 e verificar
        int dataSize = size - 1;   // O último byte recebido é o BCC2
        unsigned char bcc2 = 0;    // #BCC2: XOR calculado sobre os dados recebidos
        for (int i = 0; i < dataSize; i++)
            bcc2 ^= data[i];       // Acumula byte a byte

        unsigned char ns = cRecebido >> 7;   // C_I(n) = n << 7

        // Decidir a resposta
        if (ns != Nr)              // #DUPLICADO: testado antes do BCC2 porque esta trama já foi entregue
        {
            // Duplicado: o nosso RR perdeu-se e o emissor reenviou.
            // Confirma outra vez e descarta os dados.
            stats.duplicates++;    // Atualiza as estatísticas
            stats.rrSent++;        // Atualiza as estatísticas
            sendSupervision(A_TX, C_RR(Nr));  // #RR: repete a confirmação
            printf("I%d duplicada, descartada. RR%d enviado\n", ns, Nr);
            continue;              // Espera a trama seguinte
        }

        if (bcc2 != data[dataSize])  // #BCC2: compara o calculado com o recebido
        {
            // Trama nova mas com erro nos dados: pede retransmissão
            stats.rejSent++;       // Atualiza as estatísticas
            sendSupervision(A_TX, C_REJ(Nr));  // #REJ
            printf("I%d com erro no BCC2. REJ%d enviado\n", ns, Nr);
            continue;              // Espera a retransmissão; Nr não muda
        }

        // Trama nova e correta
        memcpy(packet, data, dataSize);  // Entrega os dados à aplicação, sem o BCC2
        Nr ^= 1;                   // #Ns_Nr: passa a esperar a trama seguinte
        stats.iReceived++;         // Atualiza as estatísticas
        stats.rrSent++;            // Atualiza as estatísticas
        sendSupervision(A_TX, C_RR(Nr));  // #RR: RRn significa "espero a trama n"
        printf("I%d recebida (%d bytes). RR%d enviado\n", ns, dataSize, Nr);
        return dataSize;           // Tamanho do pacote entregue
    }
}
////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
// #DISC #RETRANSMISSAO #ESTATISTICAS
// Emissor: envia DISC, espera o DISC do recetor, responde UA e fecha a porta.
// Devolve 0 se o fecho correu bem, -1 caso contrário.
int llCloseTx()
{
    unsigned char disc[5] = {FLAG, A_TX, C_DISC, A_TX ^ C_DISC, FLAG};  // DISC do emissor (A = 03)
    int ok = FALSE;                // Passa a TRUE se o DISC do recetor chegar

    alarmCount = 0;                // Novo contador para esta operação
    while (alarmCount <= maxRetransmissions)  // No máximo 4 envios
    {
        if (alarmCount > 0) stats.retransmissions++;  // Do 2.º envio em diante é retransmissão
        writeBytesSerialPort(disc, 5);                // Envia o DISC
        printf("DISC enviado (tentativa %d)\n", alarmCount + 1);
        alarm(timeoutSec);         // #TIMEOUT: arma o alarme
        alarmEnabled = TRUE;       // A leitura passa a respeitar o alarme

        if (receiveFrame(A_RX, C_DISC, TRUE) == 0)    // O DISC do recetor traz A = 01
        {
            alarm(0);              // Cancela o alarme
            alarmEnabled = FALSE;  // Deixa de haver alarme ativo
            printf("DISC recebido\n");
            ok = TRUE;             // Regista o sucesso
            break;                 // Sai do ciclo
        }
    }

    if (ok)
    {
        sendSupervision(A_RX, C_UA);  // Último passo: UA com A = 01
        printf("UA enviado. Ligação terminada\n");
        sleep(1);   // dá tempo a que o UA saia antes de fechar
    }
    else
        printf("Sem DISC do recetor após %d tentativas\n", alarmCount);

    printStats("tx");              // #ESTATISTICAS: imprime sempre, com ou sem sucesso

    if (closeSerialPort() < 0)     // Repõe a configuração e fecha a porta
    {
        perror("closeSerialPort");
        return -1;
    }
    return ok ? 0 : -1;            // 0 só se o DISC do recetor tiver chegado
}

// #DISC #DUPLICADO #RETRANSMISSAO #ESTATISTICAS
// Recetor: espera DISC, responde com DISC, espera o UA final e fecha a porta.
// Devolve 0 se o fecho correu bem, -1 caso contrário.
int llCloseRx()
{
    // Esperar pelo DISC. Se ainda chegar uma trama I, é um duplicado
    // (o RR da última trama perdeu-se): confirma outra vez.
    while (TRUE)
    {
        int c = readSupervision(A_TX, FALSE);  // Lê sem timeout
        if (c == C_DISC)                       // Chegou o DISC do emissor
            break;
        if (c == C_I(0) || c == C_I(1))        // #DUPLICADO: tipicamente o END repetido
        {
            stats.duplicates++;                // Atualiza as estatísticas
            stats.rrSent++;                    // Atualiza as estatísticas
            sendSupervision(A_TX, C_RR(Nr));   // #RR: repete a confirmação
            printf("I%d duplicada, descartada. RR%d enviado\n", c >> 7, Nr);
        }
    }
    printf("DISC recebido\n");

    // Responder com DISC, protegido por timer como os outros comandos
    unsigned char disc[5] = {FLAG, A_RX, C_DISC, A_RX ^ C_DISC, FLAG};  // DISC do recetor (A = 01)
    int ok = FALSE;                // Passa a TRUE se o UA final chegar

    alarmCount = 0;                // Novo contador para esta operação
    while (alarmCount <= maxRetransmissions)  // No máximo 4 envios
    {
        if (alarmCount > 0) stats.retransmissions++;  // Do 2.º envio em diante é retransmissão
        writeBytesSerialPort(disc, 5);                // Envia o DISC
        printf("DISC enviado (tentativa %d)\n", alarmCount + 1);
        alarm(timeoutSec);         // #TIMEOUT: arma o alarme
        alarmEnabled = TRUE;       // A leitura passa a respeitar o alarme

        if (receiveFrame(A_RX, C_UA, TRUE) == 0)      // O UA final traz A = 01
        {
            alarm(0);              // Cancela o alarme
            alarmEnabled = FALSE;  // Deixa de haver alarme ativo
            printf("UA recebido. Ligação terminada\n");
            ok = TRUE;             // Regista o sucesso
            break;                 // Sai do ciclo
        }
    }

    if (!ok)                       // Sem UA: o ficheiro já está guardado, só o fecho falhou
        printf("Sem UA do emissor após %d tentativas\n", alarmCount);

    printStats("rx");              // #ESTATISTICAS: imprime sempre, com ou sem sucesso

    if (closeSerialPort() < 0)     // Repõe a configuração e fecha a porta
    {
        perror("closeSerialPort");
        return -1;
    }
    return ok ? 0 : -1;            // 0 só se o UA final tiver chegado
}
