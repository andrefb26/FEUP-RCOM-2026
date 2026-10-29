// RCOM 2026/2027
//
// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source
#define BUF_SIZE 256

#define FALSE 0
#define TRUE 1

#define FLAG  0x7E
#define A_TX  0x03
#define C_SET 0x03
#define C_UA  0x07
#define A_RX   0x01
#define C_DISC 0x0B

#define ESC   0x7D
#define C_I(n)   ((unsigned char)((n) << 7))   // I0 = 0x00, I1 = 0x80
#define C_RR(n)  ((unsigned char)(0xAA | (n))) // RR0 = 0xAA, RR1 = 0xAB
#define C_REJ(n) ((unsigned char)(0x54 | (n))) // REJ0 = 0x54, REJ1 = 0x55

#define MAX_PAYLOAD 1000
#define MAX_FRAME (4 + 2 * (MAX_PAYLOAD + 1) + 1)

int alarmEnabled = FALSE;

static int timeoutSec = 3;
static int maxRetransmissions = 3;
static unsigned char Ns = 0;   // número da próxima trama I a enviar
static unsigned char Nr = 0;   // número da trama I que o recetor espera

int alarmCount = 0;

// Estatísticas do protocolo, impressas no llClose
static struct
{
    int iSent;            // tramas I enviadas (contando retransmissões)
    int retransmissions;  // tramas (SET, I, DISC) reenviadas
    int timeouts;         // alarmes disparados
    int rrReceived;
    int rejReceived;
    int iReceived;        // tramas I novas aceites
    int duplicates;       // tramas I duplicadas descartadas
    int rrSent;
    int rejSent;
} stats;

typedef enum { START, FLAG_RCV, A_RCV, C_RCV, BCC_OK, STOP, DATA, DATA_ESC } State;

// Lê uma trama [FLAG, A, C, A^C, FLAG] com endereço a e devolve o C recebido.
// Se for o cabeçalho válido de uma trama I, devolve logo o C sem ler os dados.
// Devolve -1 se o alarme disparou (só com useTimeout)
static int readSupervision(unsigned char a, int useTimeout)
{
    State state = START;
    unsigned char byte;
    unsigned char cRecebido = 0;

    while (state != STOP)
    {
        if (useTimeout && !alarmEnabled)
            return -1;

        if (readByteSerialPort(&byte) != 1)
            continue;

        switch (state)
        {
        case START:
            if (byte == FLAG) state = FLAG_RCV;
            break;
        case FLAG_RCV:
            if (byte == a) state = A_RCV;
            else if (byte != FLAG) state = START;
            break;
        case A_RCV:
            if (byte == FLAG) state = FLAG_RCV;
            else
            {
                cRecebido = byte;
                state = C_RCV;
            }
            break;
        case C_RCV:
            if (byte == (a ^ cRecebido))
            {
                if (cRecebido == C_I(0) || cRecebido == C_I(1))
                    return cRecebido;
                state = BCC_OK;
            }
            else if (byte == FLAG) state = FLAG_RCV;
            else state = START;   // BCC1 errado: ignora a trama
            break;
        case BCC_OK:
            if (byte == FLAG) state = STOP;
            else state = START;
            break;
        default:
            break;
        }
    }

    return cRecebido;
}

// Espera por uma trama com A = a e C = c (ignora as outras)
// Devolve 0 se recebeu a trama, -1 se o alarme disparou (só com useTimeout)
int receiveFrame(unsigned char a, unsigned char c, int useTimeout)
{
    while (TRUE)
    {
        int resp = readSupervision(a, useTimeout);
        if (resp == -1) return -1;
        if (resp == c) return 0;
    }
}

// Espera por um RR ou REJ. Devolve o C recebido ou -1 se o alarme disparou
int receiveSupervision(unsigned char a)
{
    while (TRUE)
    {
        int resp = readSupervision(a, TRUE);
        if (resp == -1 || resp == C_RR(0) || resp == C_RR(1) ||
            resp == C_REJ(0) || resp == C_REJ(1))
            return resp;
    }
}

static void sendSupervision(unsigned char a, unsigned char c)
{
    unsigned char frame[5] = {FLAG, a, c, a ^ c, FLAG};
    writeBytesSerialPort(frame, 5);
}

static int stuffByte(unsigned char b, unsigned char *out)
{
    if (b == FLAG || b == ESC)
    {
        out[0] = ESC;
        out[1] = b ^ 0x20;
        return 2;
    }
    out[0] = b;
    return 1;
}

void alarmHandler(int signal)
{
    alarmEnabled = FALSE;
    alarmCount++;
    stats.timeouts++;

    printf("Alarm #%d received\n", alarmCount);
}

static void installAlarm()
{
    struct sigaction act = {0};
    act.sa_handler = &alarmHandler;
    if (sigaction(SIGALRM, &act, NULL) == -1)
    {
        perror("sigaction");
        exit(1);
    }
}

static void printStats(const char *role)
{
    printf("\n--- Estatísticas (%s) ---\n", role);
    if (role[0] == 't')
    {
        printf("Tramas I enviadas:   %d\n", stats.iSent);
        printf("Retransmissões:      %d\n", stats.retransmissions);
        printf("Timeouts:            %d\n", stats.timeouts);
        printf("RR recebidos:        %d\n", stats.rrReceived);
        printf("REJ recebidos:       %d\n", stats.rejReceived);
    }
    else
    {
        printf("Tramas I aceites:    %d\n", stats.iReceived);
        printf("Duplicados:          %d\n", stats.duplicates);
        printf("RR enviados:         %d\n", stats.rrSent);
        printf("REJ enviados:        %d\n", stats.rejSent);
        printf("Retransmissões:      %d\n", stats.retransmissions);
        printf("Timeouts:            %d\n", stats.timeouts);
    }
    printf("-------------------------\n");
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llOpenTx(LinkLayer llParameters)
{
    timeoutSec = llParameters.timeout;
    maxRetransmissions = llParameters.nRetransmissions;

    if (openSerialPort(llParameters.serialPort, llParameters.baudRate) < 0)
    {
        perror("openSerialPort");
        return -1;
    }

    printf("Serial port %s opened\n", llParameters.serialPort);

    installAlarm();

    unsigned char set[5] = {FLAG, A_TX, C_SET, A_TX ^ C_SET, FLAG};
    alarmCount = 0;

    while (alarmCount <= llParameters.nRetransmissions)
    {
        // Enviar SET
        if (alarmCount > 0) stats.retransmissions++;
        writeBytesSerialPort(set, 5);
        printf("SET enviado (tentativa %d)\n", alarmCount + 1);

        alarm(llParameters.timeout); // Set alarm to be triggered after timeout
        alarmEnabled = TRUE;

        // Receber UA
        if (receiveFrame(A_TX, C_UA, TRUE) == 0)
        {
            alarm(0); // Cancel alarm
            alarmEnabled = FALSE;
            printf("UA recebido. Ligação estabelecida\n");
            return 0;
        }
    }

    printf("Sem resposta após %d tentativas\n", alarmCount);

    // Close serial port
    if (closeSerialPort() < 0)
    {
        perror("closeSerialPort");
        return -1;
    }

    printf("Serial port %s closed\n", llParameters.serialPort);

    return -1;
}

int llOpenRx(LinkLayer llParameters)
{
    timeoutSec = llParameters.timeout;
    maxRetransmissions = llParameters.nRetransmissions;

    if (openSerialPort(llParameters.serialPort, llParameters.baudRate) < 0)
    {
        perror("openSerialPort");
        return -1;
    }

    printf("Serial port %s opened\n", llParameters.serialPort);

    installAlarm();   // usado no llCloseRx para retransmitir o DISC

    // Receber SET
    receiveFrame(A_TX, C_SET, FALSE);
    printf("SET recebido\n");

    // Responder com UA
    sendSupervision(A_TX, C_UA);
    printf("UA enviado. Ligação estabelecida\n");

    // nao fechei o port para o caso de enviar e receber dados depois

    return 0;
}

////////////////////////////////////////////////
// LLSEND
////////////////////////////////////////////////
int llSend(const unsigned char *buf, int bufSize)
{
    if (buf == NULL || bufSize <= 0 || bufSize > MAX_PAYLOAD)
        return -1;

    unsigned char frame[MAX_FRAME];
    int len = 0;
    unsigned char c = C_I(Ns);

    // Cabeçalho
    frame[len++] = FLAG;
    frame[len++] = A_TX;
    frame[len++] = c;
    frame[len++] = A_TX ^ c;   // BCC1

    // Dados + BCC2
    unsigned char bcc2 = 0;
    for (int i = 0; i < bufSize; i++)
    {
        bcc2 ^= buf[i];
        len += stuffByte(buf[i], &frame[len]);
    }
    len += stuffByte(bcc2, &frame[len]);

    // Fim
    frame[len++] = FLAG;

    alarmCount = 0;
    int enviadas = 0;
    while (alarmCount <= maxRetransmissions)
    {
        if (enviadas > 0) stats.retransmissions++;
        writeBytesSerialPort(frame, len);
        stats.iSent++;
        enviadas++;
        printf("I%d enviada (%d bytes, tentativa %d)\n", Ns, bufSize, enviadas);

        alarm(timeoutSec);
        alarmEnabled = TRUE;

        // Um RR(Ns) é um RR atrasado de uma trama anterior: continua à espera
        int resp;
        do
        {
            resp = receiveSupervision(A_TX);
        } while (resp == C_RR(Ns));

        if (resp == -1)
            continue;      // timeout: volta ao início e reenvia

        alarm(0);
        alarmEnabled = FALSE;

        if (resp == C_RR(Ns ^ 1))
        {
            stats.rrReceived++;
            printf("RR%d recebido\n", Ns ^ 1);
            Ns ^= 1;
            return len;
        }
        else if (resp == C_REJ(Ns))
        {
            stats.rejReceived++;
            printf("REJ%d recebido, a retransmitir\n", Ns);
            continue;
        }
    }
    printf("I%d sem confirmação após %d tentativas\n", Ns, enviadas);
    return -1;             // esgotou as tentativas
}

////////////////////////////////////////////////
// LLRECEIVE
////////////////////////////////////////////////
int llReceive(unsigned char *packet)
{
    unsigned char data[MAX_PAYLOAD + 1];   // dados + BCC2
    unsigned char byte;

    while (TRUE)
    {
        State state = START;
        unsigned char cRecebido = 0;
        int size = 0;

        // Ler uma trama I completa (com destuffing)
        while (state != STOP)
        {
            if (readByteSerialPort(&byte) != 1)
                continue;

            switch (state)
            {
            case START:
                if (byte == FLAG) state = FLAG_RCV;
                break;
            case FLAG_RCV:
                if (byte == A_TX) state = A_RCV;
                else if (byte != FLAG) state = START;
                break;
            case A_RCV:
                if (byte == C_I(0) || byte == C_I(1) || byte == C_SET)
                {
                    cRecebido = byte;
                    state = C_RCV;
                }
                else if (byte == FLAG) state = FLAG_RCV;
                else state = START;
                break;
            case C_RCV:
                if (byte == (A_TX ^ cRecebido))
                {
                    if (cRecebido == C_SET)
                    {
                        // O nosso UA perdeu-se e o emissor reenviou o SET
                        sendSupervision(A_TX, C_UA);
                        printf("SET repetido, UA reenviado\n");
                        state = START;
                    }
                    else
                    {
                        size = 0;
                        state = DATA;
                    }
                }
                else if (byte == FLAG) state = FLAG_RCV;
                else state = START;
                break;
            case DATA:
                if (byte == FLAG) state = STOP;
                else if (byte == ESC) state = DATA_ESC;
                else if (size < MAX_PAYLOAD + 1) data[size++] = byte;
                else state = START;   // trama demasiado grande: descarta
                break;
            case DATA_ESC:
                if (size < MAX_PAYLOAD + 1)
                {
                    data[size++] = byte ^ 0x20;
                    state = DATA;
                }
                else state = START;
                break;
            default:
                break;
            }
        }

        if (size == 0)
            continue;   // nem sequer trazia BCC2: ignora

        // Separar o BCC2 e verificar
        int dataSize = size - 1;
        unsigned char bcc2 = 0;
        for (int i = 0; i < dataSize; i++)
            bcc2 ^= data[i];

        unsigned char ns = cRecebido >> 7;   // C_I(n) = n << 7

        // Decidir a resposta
        if (ns != Nr)
        {
            // Duplicado: o nosso RR perdeu-se e o emissor reenviou.
            // Confirma outra vez e descarta os dados.
            stats.duplicates++;
            stats.rrSent++;
            sendSupervision(A_TX, C_RR(Nr));
            printf("I%d duplicada, descartada. RR%d enviado\n", ns, Nr);
            continue;
        }

        if (bcc2 != data[dataSize])
        {
            // Trama nova mas com erro nos dados: pede retransmissão
            stats.rejSent++;
            sendSupervision(A_TX, C_REJ(Nr));
            printf("I%d com erro no BCC2. REJ%d enviado\n", ns, Nr);
            continue;
        }

        // Trama nova e correta
        memcpy(packet, data, dataSize);
        Nr ^= 1;
        stats.iReceived++;
        stats.rrSent++;
        sendSupervision(A_TX, C_RR(Nr));
        printf("I%d recebida (%d bytes). RR%d enviado\n", ns, dataSize, Nr);
        return dataSize;
    }
}
////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llCloseTx()
{
    unsigned char disc[5] = {FLAG, A_TX, C_DISC, A_TX ^ C_DISC, FLAG};
    int ok = FALSE;

    alarmCount = 0;
    while (alarmCount <= maxRetransmissions)
    {
        if (alarmCount > 0) stats.retransmissions++;
        writeBytesSerialPort(disc, 5);
        printf("DISC enviado (tentativa %d)\n", alarmCount + 1);
        alarm(timeoutSec);
        alarmEnabled = TRUE;

        if (receiveFrame(A_RX, C_DISC, TRUE) == 0)
        {
            alarm(0);
            alarmEnabled = FALSE;
            printf("DISC recebido\n");
            ok = TRUE;
            break;
        }
    }

    if (ok)
    {
        sendSupervision(A_RX, C_UA);
        printf("UA enviado. Ligação terminada\n");
        sleep(1);   // dá tempo a que o UA saia antes de fechar
    }
    else
        printf("Sem DISC do recetor após %d tentativas\n", alarmCount);

    printStats("tx");

    if (closeSerialPort() < 0)
    {
        perror("closeSerialPort");
        return -1;
    }
    return ok ? 0 : -1;
}

int llCloseRx()
{
    // Esperar pelo DISC. Se ainda chegar uma trama I, é um duplicado
    // (o RR da última trama perdeu-se): confirma outra vez.
    while (TRUE)
    {
        int c = readSupervision(A_TX, FALSE);
        if (c == C_DISC)
            break;
        if (c == C_I(0) || c == C_I(1))
        {
            stats.duplicates++;
            stats.rrSent++;
            sendSupervision(A_TX, C_RR(Nr));
            printf("I%d duplicada, descartada. RR%d enviado\n", c >> 7, Nr);
        }
    }
    printf("DISC recebido\n");

    // Responder com DISC, protegido por timer como os outros comandos
    unsigned char disc[5] = {FLAG, A_RX, C_DISC, A_RX ^ C_DISC, FLAG};
    int ok = FALSE;

    alarmCount = 0;
    while (alarmCount <= maxRetransmissions)
    {
        if (alarmCount > 0) stats.retransmissions++;
        writeBytesSerialPort(disc, 5);
        printf("DISC enviado (tentativa %d)\n", alarmCount + 1);
        alarm(timeoutSec);
        alarmEnabled = TRUE;

        if (receiveFrame(A_RX, C_UA, TRUE) == 0)
        {
            alarm(0);
            alarmEnabled = FALSE;
            printf("UA recebido. Ligação terminada\n");
            ok = TRUE;
            break;
        }
    }

    if (!ok)
        printf("Sem UA do emissor após %d tentativas\n", alarmCount);

    printStats("rx");

    if (closeSerialPort() < 0)
    {
        perror("closeSerialPort");
        return -1;
    }
    return ok ? 0 : -1;
}
