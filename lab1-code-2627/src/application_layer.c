// RCOM 2026/2027
//
// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"

#include <stdio.h>
#include <string.h>
#define CHUNK_SIZE 997   // 1000 (máximo da trama) - 3 (C, L2, L1)


static int buildControlPacket(unsigned char c, long fileSize,
                              const char *name, unsigned char *pkt)
{
    int i = 0;

    pkt[i++] = c;                      // 1 = START, 3 = END

    // tamanho
    pkt[i++] = 0;                      // T: tamanho
    pkt[i++] = 4;                      // L: 4 bytes
    pkt[i++] = (fileSize >> 24) & 0xFF;
    pkt[i++] = (fileSize >> 16) & 0xFF;
    pkt[i++] = (fileSize >> 8) & 0xFF;
    pkt[i++] = fileSize & 0xFF;

    // nome
    int nameLen = strlen(name);
    pkt[i++] = 1;                      // T: nome
    pkt[i++] = nameLen;                // L: número de letras
    memcpy(&pkt[i], name, nameLen);    // V: copia as letras
    i += nameLen;

    return i;                          // tamanho total
}

static int buildDataPacket(const unsigned char *data, int size, unsigned char *pkt)
{
    pkt[0] = 2;                     // C: dados
    pkt[1] = (size >> 8) & 0xFF;    // L2
    pkt[2] = size & 0xFF;           // L1
    memcpy(&pkt[3], data, size);    // os dados
    return size + 3;                // tamanho total do pacote
}

static long parseFileSize(const unsigned char *pkt, int n)
{
    int i = 1;                       // salta o C
    while (i + 1 < n)
    {
        unsigned char T = pkt[i];
        unsigned char L = pkt[i + 1];
        if (T == 0)                  // é o tamanho
        {
            long size = 0;
            for (int k = 0; k < L; k++)
                size = size * 256 + pkt[i + 2 + k];
            return size;
        }
        i += 2 + L;                  // salta este TLV (T, L e os L bytes do valor)
    }
    return -1;
}


void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer llParameters = {
        .baudRate = baudRate,
        .nRetransmissions = nTries,
        .timeout = timeout,
    };
    strcpy(llParameters.serialPort, serialPort);

    if (strcmp(role, "tx") == 0)
    {
        // abrir o ficheiro
        FILE *f = fopen(filename, "rb");
        if (f == NULL) { perror("fopen"); return; }

        fseek(f, 0, SEEK_END);
        long fileSize = ftell(f);
        fseek(f, 0, SEEK_SET);

        if (llOpenTx(llParameters) < 0) { fclose(f); return; }

        // construir e enviar o pacote de controlo START
        unsigned char pkt[1000];
        int n = buildControlPacket(1, fileSize, filename, pkt);

        for (int k = 0; k < n; k++) printf("%02X ", pkt[k]);   // para ver o pacote
        printf("\n");

        if (llSend(pkt, n) < 0) { printf("Falhou o START\n"); fclose(f); return; }

        // construir e enviar os pacotes de dados
        unsigned char chunk[CHUNK_SIZE];
        int lidos;
        while ((lidos = fread(chunk, 1, CHUNK_SIZE, f)) > 0)
        {
            n = buildDataPacket(chunk, lidos, pkt);
            if (llSend(pkt, n) < 0)
            {
                printf("Falhou o envio de dados\n");
                fclose(f);
                return;
            }
        }

        // construir e enviar o pacote de controlo END
        n = buildControlPacket(3, fileSize, filename, pkt);
        if (llSend(pkt, n) < 0) { 
            printf("Falhou o END\n");
            fclose(f); return; 
        }

        llCloseTx();
        fclose(f);
    }
    else if (strcmp(role, "rx") == 0)
    {
        if (llOpenRx(llParameters) < 0) return;

        FILE *f = fopen(filename, "wb");     // "wb": write binary
        if (f == NULL) { perror("fopen"); llCloseRx(); return; }

        unsigned char pkt[1000];
        long fileSize = -1;
        long recebidos = 0;

        while (1)
        {
            int n = llReceive(pkt);
            if (n < 0) { printf("Erro no llReceive\n"); break; }

            if (pkt[0] == 1)                  // START
            {
                fileSize = parseFileSize(pkt, n);
                printf("START: ficheiro com %ld bytes\n", fileSize);
            }
            else if (pkt[0] == 2)             // DATA
            {
                int k = 256 * pkt[1] + pkt[2];   // L2 e L1
                fwrite(&pkt[3], 1, k, f);        // os dados começam na posição 3
                recebidos += k;
            }
            else if (pkt[0] == 3)             // END
            {
                printf("END: recebidos %ld de %ld bytes\n", recebidos, fileSize);
                break;
            }
        }

        fclose(f);
        llCloseRx();
    }

    else
    {
        printf("Invalid role: %s. Must be 'tx' or 'rx'.\n", role);
        return;
    }
}
