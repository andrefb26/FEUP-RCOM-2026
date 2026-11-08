// RCOM 2026/2027
//
// Application layer protocol implementation
//
// PAPEL DESTE FICHEIRO
// A camada de aplicação trata do FICHEIRO: lê-o e parte-o em pacotes (emissor)
// ou junta os pacotes e escreve-o (recetor). Não sabe nada de tramas, FLAGs,
// BCCs ou retransmissões: isso é tudo da camada de ligação (link_layer.c).
// Só usa llOpenTx/llOpenRx, llSend, llReceive e llCloseTx/llCloseRx.
//
// ÍNDICE (usa Ctrl+F com a etiqueta)
//   #CHUNK        tamanho de cada bocado do ficheiro (997 bytes)
//   #TLV          campos Tipo-Comprimento-Valor dos pacotes de controlo
//   #PACOTE_CTRL  construção dos pacotes START e END
//   #PACOTE_DADOS construção dos pacotes DATA
//   #LER_TAMANHO  o recetor extrai o tamanho do ficheiro do START
//   #EMISSOR      todo o fluxo do emissor
//   #RECETOR      todo o fluxo do recetor
//   #START        envio e receção do pacote START
//   #DATA         envio e receção dos pacotes de dados
//   #END          envio e receção do pacote END
//
// FORMATO DOS PACOTES
//   START: [1][T=0][L=4][4 bytes do tamanho][T=1][L=n][n bytes do nome]
//   DATA:  [2][L2][L1][dados], com tamanho dos dados = 256*L2 + L1
//   END:   [3] seguido dos mesmos campos do START

#include "application_layer.h"  // Declaração de applicationLayer()
#include "link_layer.h"         // llOpen, llSend, llReceive, llClose e struct LinkLayer

#include <stdio.h>              // FILE, fopen, fread, fwrite, fseek, ftell, printf
#include <string.h>             // strcmp, strcpy, strlen, memcpy
// #CHUNK: quantos bytes do ficheiro cabem em cada pacote DATA
#define CHUNK_SIZE 997   // 1000 (máximo da trama) - 3 (C, L2, L1)


// #PACOTE_CTRL #TLV
// Constrói um pacote de controlo (START ou END) dentro de pkt.
//   c        = 1 para START, 3 para END
//   fileSize = tamanho do ficheiro em bytes
//   name     = nome do ficheiro
//   pkt      = buffer onde o pacote é escrito
// Devolve o número de bytes do pacote.
static int buildControlPacket(unsigned char c, long fileSize,
                              const char *name, unsigned char *pkt)
{
    int i = 0;                         // i é a próxima posição livre em pkt

    pkt[i++] = c;                      // 1 = START, 3 = END

    // tamanho: primeiro campo TLV, com o tamanho em 4 bytes (big-endian)
    pkt[i++] = 0;                      // T: tamanho
    pkt[i++] = 4;                      // L: 4 bytes
    pkt[i++] = (fileSize >> 24) & 0xFF;  // byte mais significativo
    pkt[i++] = (fileSize >> 16) & 0xFF;  // segundo byte
    pkt[i++] = (fileSize >> 8) & 0xFF;   // terceiro byte
    pkt[i++] = fileSize & 0xFF;          // byte menos significativo

    // nome: segundo campo TLV, com as letras do nome do ficheiro
    int nameLen = strlen(name);        // número de letras, sem o '\0' final
    pkt[i++] = 1;                      // T: nome
    pkt[i++] = nameLen;                // L: número de letras
    memcpy(&pkt[i], name, nameLen);    // V: copia as letras
    i += nameLen;                      // avança i para depois do nome

    return i;                          // tamanho total
}

// #PACOTE_DADOS
// Constrói um pacote DATA dentro de pkt com os size bytes de data.
// Devolve o número de bytes do pacote (dados + 3 de cabeçalho).
static int buildDataPacket(const unsigned char *data, int size, unsigned char *pkt)
{
    pkt[0] = 2;                     // C: dados
    pkt[1] = (size >> 8) & 0xFF;    // L2: parte alta do tamanho
    pkt[2] = size & 0xFF;           // L1: parte baixa do tamanho
    memcpy(&pkt[3], data, size);    // os dados
    return size + 3;                // tamanho total do pacote
}

// #LER_TAMANHO #TLV
// Percorre os campos TLV do pacote START (com n bytes) à procura do tamanho.
// Devolve o tamanho do ficheiro, ou -1 se o campo não existir.
static long parseFileSize(const unsigned char *pkt, int n)
{
    int i = 1;                       // salta o C
    while (i + 1 < n)                // tem de haver pelo menos T e L para ler
    {
        unsigned char T = pkt[i];       // tipo deste campo
        unsigned char L = pkt[i + 1];   // comprimento do valor deste campo
        if (T == 0)                  // é o tamanho
        {
            long size = 0;           // valor a reconstruir
            // Big-endian: cada byte novo empurra os anteriores 8 bits para cima
            for (int k = 0; k < L; k++)
                size = size * 256 + pkt[i + 2 + k];  // o valor começa em i + 2
            return size;             // encontrou o tamanho
        }
        i += 2 + L;                  // salta este TLV (T, L e os L bytes do valor)
    }
    return -1;                       // o pacote não trazia o campo do tamanho
}


// Ponto de entrada desta camada. É chamada pelo main.c com os argumentos da
// linha de comandos e decide se este programa é o emissor ou o recetor.
//   serialPort = porta série, por exemplo /tmp/ttyS10
//   role       = "tx" (emissor) ou "rx" (recetor)
//   baudRate   = velocidade da porta
//   nTries     = número de retransmissões permitidas
//   timeout    = tempo de espera por uma resposta, em segundos
//   filename   = ficheiro a enviar (tx) ou nome a dar ao ficheiro recebido (rx)
void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    // Junta os parâmetros na struct que a camada de ligação recebe no llOpen
    LinkLayer llParameters = {
        .baudRate = baudRate,          // velocidade
        .nRetransmissions = nTries,    // retransmissões permitidas
        .timeout = timeout,            // timeout em segundos
    };
    strcpy(llParameters.serialPort, serialPort);  // serialPort é um array, copia-se com strcpy

    // #EMISSOR: lê o ficheiro e envia-o
    if (strcmp(role, "tx") == 0)       // strcmp devolve 0 quando as strings são iguais
    {
        // abrir o ficheiro
        FILE *f = fopen(filename, "rb");             // "rb": leitura em binário
        if (f == NULL) { perror("fopen"); return; }  // ficheiro inexistente ou sem permissões

        // Descobrir o tamanho: vai ao fim, pergunta a posição e volta ao início
        fseek(f, 0, SEEK_END);         // coloca a posição no fim do ficheiro
        long fileSize = ftell(f);      // a posição no fim é o tamanho em bytes
        fseek(f, 0, SEEK_SET);         // volta ao início para ler os dados

        // Fase 1: estabelecer a ligação (SET/UA). Se falhar, liberta o ficheiro e sai
        if (llOpenTx(llParameters) < 0) { fclose(f); return; }

        // #START
        // construir e enviar o pacote de controlo START
        unsigned char pkt[1000];                          // buffer do pacote: máximo de 1000 bytes
        int n = buildControlPacket(1, fileSize, filename, pkt);  // n = tamanho do pacote

        for (int k = 0; k < n; k++) printf("%02X ", pkt[k]);   // para ver o pacote
        printf("\n");                                          // termina a linha do hexadecimal

        // llSend só devolve depois de receber confirmação, ou -1 se desistir
        if (llSend(pkt, n) < 0) { printf("Falhou o START\n"); fclose(f); return; }

        // #DATA
        // construir e enviar os pacotes de dados
        unsigned char chunk[CHUNK_SIZE];   // guarda o bocado do ficheiro lido em cada volta
        int lidos;                         // quantos bytes o fread leu de facto
        // fread devolve 0 no fim do ficheiro, e é isso que termina o ciclo.
        // A última leitura pode trazer menos de CHUNK_SIZE bytes.
        while ((lidos = fread(chunk, 1, CHUNK_SIZE, f)) > 0)
        {
            n = buildDataPacket(chunk, lidos, pkt);  // junta o cabeçalho C, L2, L1
            if (llSend(pkt, n) < 0)                  // envia e espera pela confirmação
            {
                printf("Falhou o envio de dados\n");  // esgotaram-se as tentativas
                fclose(f);                            // liberta o ficheiro
                return;                               // sai sem fazer llClose
            }
        }

        // #END
        // construir e enviar o pacote de controlo END
        n = buildControlPacket(3, fileSize, filename, pkt);  // igual ao START, mas com C = 3
        if (llSend(pkt, n) < 0) {
            printf("Falhou o END\n");   // o recetor não confirmou o fim
            fclose(f); return;          // liberta o ficheiro e sai
        }

        // Fase final: terminar a ligação (DISC, DISC, UA) e imprimir estatísticas
        llCloseTx();
        fclose(f);                      // fecha o ficheiro lido
    }
    // #RECETOR: recebe os pacotes e escreve o ficheiro
    else if (strcmp(role, "rx") == 0)
    {
        // Fase 1: espera pelo SET e responde com UA
        if (llOpenRx(llParameters) < 0) return;

        // Cria o ficheiro de destino; o nome vem da linha de comandos, não do START
        FILE *f = fopen(filename, "wb");     // "wb": write binary
        if (f == NULL) { perror("fopen"); llCloseRx(); return; }  // não foi possível criar

        unsigned char pkt[1000];             // buffer de cada pacote recebido
        long fileSize = -1;                  // tamanho anunciado no START (-1 = ainda não chegou)
        long recebidos = 0;                  // bytes de dados recebidos até agora

        // Recebe pacotes até chegar o END; pkt[0] é o campo C e indica o tipo
        while (1)
        {
            // Só devolve com um pacote novo e correto; duplicados e erros
            // são resolvidos dentro de llReceive
            int n = llReceive(pkt);
            if (n < 0) { printf("Erro no llReceive\n"); break; }  // erro na camada de ligação

            // #START
            if (pkt[0] == 1)                  // START
            {
                fileSize = parseFileSize(pkt, n);  // guarda o tamanho anunciado
                printf("START: ficheiro com %ld bytes\n", fileSize);  // mostra-o na consola
            }
            // #DATA
            else if (pkt[0] == 2)             // DATA
            {
                int k = 256 * pkt[1] + pkt[2];   // L2 e L1
                fwrite(&pkt[3], 1, k, f);        // os dados começam na posição 3
                recebidos += k;                  // atualiza o total recebido
            }
            // #END
            else if (pkt[0] == 3)             // END
            {
                printf("END: recebidos %ld de %ld bytes\n", recebidos, fileSize);  // só mostra, não compara
                break;                        // terminou: sai do ciclo
            }
        }

        fclose(f);      // fecha o ficheiro recebido, que fica guardado em disco
        llCloseRx();    // Fase final: espera DISC, responde DISC e espera UA
    }

    // Só chega aqui com um papel inválido; o main.c já valida isto antes
    else
    {
        printf("Invalid role: %s. Must be 'tx' or 'rx'.\n", role);
        return;
    }
}
