// RCOM 2026/2027
//
// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source
#define BUF_SIZE 256

#define FALSE 0
#define TRUE 1

#define FLAG  0x7E
#define A_TX  0x03
#define C_SET 0x03
#define C_UA  0x07

int alarmEnabled = FALSE;
int alarmCount = 0;

typedef enum { START, FLAG_RCV, A_RCV, C_RCV, BCC_OK, STOP } State;

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
    // ----------------------------------------------------
    // This example code shows how to open the serial port and send a string.
    // TODO: Adapt and extend this code according to the specifications of the project.
    // ----------------------------------------------------

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
    // ----------------------------------------------------
    // This example code shows how to open the serial port and receive a string.
    // TODO: Adapt and extend this code according to the specifications of the project.
    // ----------------------------------------------------

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

    // Close serial port
    if (closeSerialPort() < 0)
    {
        perror("closeSerialPort");
        return -1;
    }

    printf("Serial port %s closed\n", llParameters.serialPort);

    return 0;
}

////////////////////////////////////////////////
// LLSEND
////////////////////////////////////////////////
int llSend(const unsigned char *buf, int bufSize)
{
    // TODO: Implement this function

    return 0;
}

////////////////////////////////////////////////
// LLRECEIVE
////////////////////////////////////////////////
int llReceive(unsigned char *packet)
{
    // TODO: Implement this function

    return 0;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llCloseTx()
{
    // TODO: Implement this function

    return 0;
}

int llCloseRx()
{
    // TODO: Implement this function

    return 0;
}
