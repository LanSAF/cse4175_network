#include <stdio.h>
#include <math.h>
#include "netsim.h"

// tunable parameters
#ifndef P_INIT
#define P_INIT      500
#endif
#ifndef P_MIN
#define P_MIN       32
#endif
#ifndef WINDOW_SIZE
#define WINDOW_SIZE 150
#endif
#ifndef WARM_UP
#define WARM_UP     25
#endif

// fixed parameters
#define P_MAX       65535
#define GENERATOR   0x04C11DB7      // CRC-32 generator polynomial g(x), lower 32 bits

// sliding window for BER estimation: stores the last WINDOW_SIZE frames' results
int win_bits[WINDOW_SIZE] = {};
int win_nak[WINDOW_SIZE] = {};
int win_idx = 0;
int win_cnt = 0;
int total_nak = 0;
long long total_bits = 0;

void build_frame (uint8_t *payload, size_t N, uint8_t *frame);
uint32_t compute_crc32 (uint8_t *frame, size_t N);
void update_window (int frame_bits, int was_nak);
int compute_P ();

int main (int argc, char* argv[])
{
    if (argc < 2) return 1;
    
    int P = P_INIT;
    uint8_t buf[P_MAX];
    uint8_t frame[P_MAX + 6];

    // open the input file
    FILE *fp = fopen (argv[1], "rb");
    if (fp == NULL) return 1;
    
    while (1)
    {
        // read P bytes for a payload
        size_t N = fread (buf, 1, P, fp); 
        if (N == 0) break;

        // build and send the frame
        build_frame (buf, N, frame);
        int result = send_frame (frame, N + 6);

        
        if (result == NETSIM_ACK)
        {
            update_window ((int) (N + 6) * 8, 0);
            P = compute_P ();
        }
        else if (result == NETSIM_NAK)
        {
            update_window ((int) (N + 6) * 8, 1);
            fseek(fp, -(long)N, SEEK_CUR);          // rewind to retransmit the same payload
            P = compute_P ();
        }
        else    // NETSIM_ERROR
        {
            fclose (fp);
            return 1;
        }
    }

    fclose (fp);
    return 0;
}

// frame format: [size: 2 bytes] [payload: N bytes] [CRC-32: 4 bytes]
void build_frame (uint8_t *payload, size_t N, uint8_t *frame)
{
    // 2 bytes for the size of payload
    frame[0] = (N >> 8) & 0xFF;
    frame[1] = N & 0xFF;

    // N bytes for the content of payload
    for (int i = 0; i < N; i++)
    {
        frame[i + 2] = payload[i];
    }

    // 4 bytes for the remainder from CRC-32, computed over size field + payload
    uint32_t crc = compute_crc32(frame, N + 2);
    frame[N + 2] = (crc >> 24) & 0xFF;
    frame[N + 3] = (crc >> 16) & 0xFF;
    frame[N + 4] = (crc >> 8) & 0xFF;
    frame[N + 5] =  crc & 0xFF;
}

// modulo-2 division: processes one byte at a time
uint32_t compute_crc32 (uint8_t *frame, size_t N)
{
    uint32_t remainder = 0;

    for (size_t i = 0; i < N; i++)
    {
        remainder ^= ((uint32_t) frame[i] << 24);           // load a byte into the top 8 bits
        for (int bit = 0; bit < 8; bit++)
        {
            if (remainder & 0x80000000)
            {
                remainder = (remainder << 1) ^ GENERATOR;   // MSB=1: divide(=XOR) with generator
            }
            else
            {
                remainder <<= 1;                            // MSB=0: divide(=XOR) with 0
            }
        }
    }

    return remainder;
}

void update_window (int frame_bits, int was_nak)
{
    if (win_cnt == WINDOW_SIZE)
    {
        // remove the oldest entry to maintain fixed window size
        total_nak -= win_nak[win_idx];
        total_bits -= win_bits[win_idx];
    }
    else
    {
        win_cnt++;
    }

    // insert new frame's info
    win_nak[win_idx] = was_nak;
    win_bits[win_idx] = frame_bits;
    total_nak += was_nak;
    total_bits += frame_bits;
    
    win_idx = (win_idx + 1) % WINDOW_SIZE;
}

int compute_P ()
{
    // use P_INIT until enough samples are collected for a reliable BER estimate (Warming up)
    if (win_cnt < WARM_UP) return P_INIT;
    
    // get NAK rate from the sliding window
    double nak_rate = (double) total_nak / win_cnt;
    if (nak_rate <= 0.0) return P_MAX;
    if (nak_rate >= 1.0) return P_MIN;

    /* BER estimate from NAK rate:
       nak_rate = 1 - (1-BER)^avg_bits  =>  BER = -ln(1 - nak_rate) / avg_bits         */
    double avg_bits = (double) total_bits / win_cnt;
    double BER_est = -log (1.0 - nak_rate) / avg_bits;

    /* optimal payload size derived by minimizing cost
       cost = bytes_total + K * frames_total                                            */
    double P_opt = (sqrt (65536.0 + 128.0 / BER_est) - 256.0) / 2.0;

    int P = (int) round (P_opt);
    if (P < P_MIN) return P_MIN;
    if (P > P_MAX) return P_MAX;

    return P;
}