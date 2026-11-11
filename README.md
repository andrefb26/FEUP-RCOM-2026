# Reliable File Transfer over a Serial Port

A data link protocol written in C that reliably transfers a file between two computers over an RS-232 serial port, even when the cable drops bytes, corrupts them or disconnects for a while.

Developed for the **Computer Networks** course (L.EIC025) of the Bachelor in Informatics and Computing Engineering at FEUP (University of Porto), 2026/27.

**Authors:** André Baptista ([@andrefb26](https://github.com/andrefb26)) and Leonor Alpoim ([@leonoralpoim](https://github.com/leonoralpoim))

> Work in progress: this is an ongoing course project.

## How it works

The program is split into two layers. The application layer only calls `llOpen`, `llSend`, `llReceive` and `llClose`, and never has to deal with frames or errors.

```
 Application layer   file  ->  START packet | DATA packets | END packet
        |
 Link layer          framing, error detection, acknowledgements, retransmission
        |
 Serial port         RS-232 (real or virtual cable)
```

### Link layer (`src/link_layer.c`)

- **Framing:** every frame is delimited by `FLAG` bytes (`0x7E`) and has an address, a control field and a header checksum (BCC1 = A XOR C).
- **Byte stuffing:** `FLAG` and `ESC` bytes inside the payload are escaped (`ESC`, byte XOR `0x20`), so data can never be confused with a frame boundary.
- **Error detection:** information frames also carry a data checksum (BCC2, XOR of all payload bytes).
- **Frame parsing:** the receiver reads the port byte by byte with explicit state machines that resynchronise after malformed input and remove stuffing on the fly.
- **Stop-and-Wait ARQ** with alternating sequence numbers (0/1):
  - a corrupted frame is answered with `REJ` and sent again;
  - a lost frame or acknowledgement triggers a retransmission when a timer set with POSIX signals (`sigaction` / `SIGALRM`) expires, up to a configurable number of retries;
  - a duplicate frame (when an `RR` was lost) is detected by its sequence number, acknowledged again and discarded.
- **Connection management:** the connection is opened with a `SET` / `UA` handshake and closed with `DISC` / `DISC` / `UA`.

### Application layer (`src/application_layer.c`)

- The transmitter sends a **START** control packet with the file size and name encoded as TLV (type-length-value) fields, then the file in **DATA** packets of up to 997 bytes, then an **END** control packet.
- The receiver writes each DATA packet to disk and rebuilds the file byte for byte.

## Project structure

```
lab1-code-2627/
├── src/
│   ├── link_layer.c/.h         # data link protocol (our implementation)
│   ├── application_layer.c/.h  # file transfer application (our implementation)
│   ├── serial_port.c/.h        # low-level serial port access (provided)
│   └── main.c                  # argument parsing and entry point (provided)
├── cable/cable.c               # virtual cable with noise/disconnection simulation (provided)
├── penguin.gif                 # sample file to transfer
└── Makefile
```

The files marked *provided* are part of the course's base code, including the virtual cable written by the course staff. The link layer and the application layer are our implementation.

## Building and running

Requirements: Linux, `gcc`, `make` and `socat` (for the virtual cable).

```bash
cd lab1-code-2627
make                 # builds bin/main and bin/cable

make run_cable       # terminal 1: virtual cable between /tmp/ttyS10 and /tmp/ttyS11
make run_rx          # terminal 2: receiver  -> penguin-received.gif
make run_tx          # terminal 3: transmitter <- penguin.gif

make check_files     # checks that the received file is identical to the original
```

Or run the program directly:

```bash
./bin/main <serial port> <baud rate> tx|rx <file>
./bin/main /tmp/ttyS11 9600 rx penguin-received.gif
./bin/main /tmp/ttyS10 9600 tx penguin.gif
```

To test error recovery, use the cable program's console during a transfer to disconnect the cable or add noise. The file should still arrive intact.
