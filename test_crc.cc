#include <stdio.h>
#include <stdint.h>
#include <assert.h>

#define GENERATOR 0x04C11DB7

uint32_t compute_crc32(uint8_t *data, size_t N)
{
    uint32_t remainder = 0;
    for (size_t i = 0; i < N; i++) {
        remainder ^= ((uint32_t)data[i] << 24);
        for (int bit = 0; bit < 8; bit++) {
            if (remainder & 0x80000000)
                remainder = (remainder << 1) ^ GENERATOR;
            else
                remainder <<= 1;
        }
    }
    return remainder;
}

int test(const char *label, uint8_t *data, size_t data_len)
{
    uint32_t crc = compute_crc32(data, data_len);
    data[data_len + 0] = (crc >> 24) & 0xFF;
    data[data_len + 1] = (crc >> 16) & 0xFF;
    data[data_len + 2] = (crc >>  8) & 0xFF;
    data[data_len + 3] =  crc        & 0xFF;

    uint32_t check = compute_crc32(data, data_len + 4);
    printf("[%s]  CRC=0x%08X  check=0x%08X  %s\n",
        label, crc, check, check == 0 ? "PASS" : "FAIL");
    return check == 0;
}

int main()
{
    int all_pass = 1;

    // 케이스 1: 일반 payload
    uint8_t d1[32] = {0x00, 0x06, 'H', 'e', 'l', 'l', 'o', '!'};
    all_pass &= test("short string ", d1, 8);

    // 케이스 2: 모든 바이트 0x00
    uint8_t d2[32] = {};
    all_pass &= test("all zeros    ", d2, 8);

    // 케이스 3: 모든 바이트 0xFF
    uint8_t d3[32];
    for (int i = 0; i < 28; i++) d3[i] = 0xFF;
    all_pass &= test("all 0xFF     ", d3, 28);

    // 케이스 4: 1바이트 payload (최소 크기)
    uint8_t d4[8] = {0x00, 0x01, 0xAB};
    all_pass &= test("1-byte payload", d4, 3);

    printf("\n%s\n", all_pass ? "전체 통과" : "실패 있음");
    return all_pass ? 0 : 1;
}