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

typedef enum { START, FLAG_RCV, A_RCV, C_RCV, BCC_OK, STOP, DATA, DATA_ESC } State;

// Lê bytes até receber uma trama [FLAG, A, C, A^C, FLAG] válida
// Devolve 0 se recebeu a trama, -1 se o alarme disparou (só com useTimeout)
int receiveFrame(unsigned char a, unsigned char c, int useTimeout)
{
    State state = START;
    unsigned char byte;

    while (state != STOP)
    {
        if (useTimeout && !alarmEnabled)
            return -1;

        if (readByteSerialPort(&byte) != 1)
            continue;

        printf("0x%02X\n", byte);

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
            if (byte == c) state = C_RCV;
            else if (byte == FLAG) state = FLAG_RCV;
            else state = START;
            break;
        case C_RCV:
            if (byte == (a ^ c)) state = BCC_OK;
            else if (byte == FLAG) state = FLAG_RCV;
            else state = START;
            break;
        case BCC_OK:
            if (byte == FLAG) state = STOP;
            else state = START;
            break;
        default:
            break;
        }
    }

    return 0;
}

int receiveSupervision(unsigned char a){

    State state = START;
    unsigned char byte;
    unsigned char cRecebido = 0;

    while (state != STOP)
    {
        if (!alarmEnabled) return -1;

        if (readByteSerialPort(&byte) != 1)
            continue;

        printf("0x%02X\n", byte);

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
            if (byte == C_RR(0) || byte == C_RR(1) || byte == C_REJ(0) || byte == C_REJ(1)) {
                
                cRecebido = byte;
                state = C_RCV;
            }
            else if (byte == FLAG) state = FLAG_RCV;
            else state = START;
            break;
        case C_RCV:
            if (byte == (a ^ cRecebido)) state = BCC_OK;
            else if (byte == FLAG) state = FLAG_RCV;
            else state = START;
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

static void sendSupervision(unsigned char c)
{
    unsigned char frame[5] = {FLAG, A_TX, c, A_TX ^ c, FLAG};
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

    printf("Alarm #%d received\n", alarmCount);
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

    // Configurar
    struct sigaction act = {0};
    act.sa_handler = &alarmHandler;
    if (sigaction(SIGALRM, &act, NULL) == -1)
    {
        perror("sigaction");
        exit(1);
    }
    
    unsigned char set[5] = {FLAG, A_TX, C_SET, A_TX ^ C_SET, FLAG};
    alarmCount = 0;


    

    while (alarmCount <= llParameters.nRetransmissions)
    {
        // Enviar SET
        int bytes = writeBytesSerialPort(set, 5);
        printf("SET enviado (%d bytes)\n", bytes);
        
        alarm(llParameters.timeout); // Set alarm to be triggered after timeout
        alarmEnabled = TRUE;

        // Receber UA
        if (receiveFrame(A_TX, C_UA, TRUE) == 0)
        {

            alarm(0); // Cancel alarm
            alarmEnabled = FALSE;
            printf("UA recebido corretamente. Ligação estabelecida\n");
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
   
    if (openSerialPort(llParameters.serialPort, llParameters.baudRate) < 0)
    {
        perror("openSerialPort");
        return -1;
    }

    printf("Serial port %s opened\n", llParameters.serialPort);

    // Receber SET
    receiveFrame(A_TX, C_SET, FALSE);
    printf("SET recebido corretamente\n");

    // Responder com UA
    unsigned char ua[5] = {FLAG, A_TX, C_UA, A_TX ^ C_UA, FLAG};
    int bytes = writeBytesSerialPort(ua, 5);
    printf("UA enviado (%d bytes)\n", bytes);

    // Esperar que todos os bytes sejam escritos
    sleep(1);

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
    while (alarmCount <= maxRetransmissions)
    {
        writeBytesSerialPort(frame, len);
        alarm(timeoutSec);
        alarmEnabled = TRUE;

        int resp = receiveSupervision(A_TX);
        if (resp == -1)
            continue;      // timeout: volta ao início e reenvia

        alarm(0);
        alarmEnabled = FALSE;

        if (resp == C_RR(Ns ^ 1))
        {
            Ns ^= 1;
            return len;
        }
        else if (resp == C_REJ(Ns))
        {
            continue;
        }
    }
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
                if (byte == C_I(0) || byte == C_I(1))
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
                    size = 0;
                    state = DATA;
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
            sendSupervision(C_RR(Nr));
            continue;
        }

        if (bcc2 != data[dataSize])
        {
            // Trama nova mas com erro nos dados: pede retransmissão
            sendSupervision(C_REJ(Nr));
            continue;
        }

        // Trama nova e correta
        memcpy(packet, data, dataSize);
        Nr ^= 1;
        sendSupervision(C_RR(Nr));
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
        writeBytesSerialPort(disc, 5);
        alarm(timeoutSec);
        alarmEnabled = TRUE;

        if (receiveFrame(A_RX, C_DISC, TRUE) == 0)
        {
            alarm(0);
            alarmEnabled = FALSE;
            ok = TRUE;
            break;
        }
    }

    if (ok)
    {
        unsigned char ua[5] = {FLAG, A_RX, C_UA, A_RX ^ C_UA, FLAG};
        writeBytesSerialPort(ua, 5);
        sleep(1);   // dá tempo a que o UA saia antes de fechar
    }

    if (closeSerialPort() < 0)
    {
        perror("closeSerialPort");
        return -1;
    }
    return ok ? 0 : -1;
}

int llCloseRx()
{
    receiveFrame(A_TX, C_DISC, FALSE);

    unsigned char disc[5] = {FLAG, A_RX, C_DISC, A_RX ^ C_DISC, FLAG};
    writeBytesSerialPort(disc, 5);

    receiveFrame(A_RX, C_UA, FALSE);

    if (closeSerialPort() < 0)
    {
        perror("closeSerialPort");
        return -1;
    }
    return 0;
}
