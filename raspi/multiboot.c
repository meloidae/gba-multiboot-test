#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <wiringPi.h>

#define SLEEP_DURATION 10000

uint32_t writeSPI32NoMessage(uint32_t write_bits) {
    uint8_t buffer[4];

    // Construct bits to write
    buffer[3] = write_bits & 0x000000ff;
    buffer[2] = (write_bits & 0x0000ff00) >> 8;
    buffer[1] = (write_bits & 0x00ff0000) >> 16;
    buffer[0] = (write_bits & 0xff000000) >> 24;

    // write/read
    wiringPiSPIDataRW(0, &buffer, 4);

    uint32_t read_bits = 0;
    read_bits |= buffer[0] << 24;
    read_bits |= buffer[1] << 16;
    read_bits |= buffer[2] << 8;
    read_bits |= buffer[3];

    return read_bits;
} // writeSPI32NoMessage

uint32_t writeSPI32(uint32_t write_bits, char *message) {
    uint32_t read_bits = writeSPI32NoMessage(write_bits);

    fprintf(stdout, "sent: 0x%08x, received: 0x%08x; %s\n", write_bits, read_bits, message);

    return read_bits;
} // writeSPI32

uint32_t waitSPI32(uint32_t write_bits, uint32_t compare_bits, char *message) {
    fprintf(stdout, "%s 0x%08x\n", message, compare_bits); 
    uint32_t read_bits;

    while(1) {
        read_bits = writeSPI32NoMessage(write_bits);
        usleep(SLEEP_DURATION);
        if (read_bits != compare_bits) {
            continue;
        } // if 
        break;
    } // while
} // waitSPI32



int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Please provide the location of multiboot gba file");
        exit(1);
    } // if
    char *mb_filename = argv[1];

    // Open gba file to multiboot
    FILE *fp = fopen(mb_filename, "rb");

    if (fp == NULL) {
        fprintf(stderr, "Failed to open file: %s", mb_filename);
        exit(1);
    } // if

    // Measure file size (should be < 256kb)
    fseek(fp, 0L, SEEK_END);
    long file_size = (ftell(fp) + 0x0f) & 0xfffffff0;

    if (file_size > 0x40000) {
        fprintf(stderr, "File too big: max file size is 256kb");
        exit(1);
    } // if

    // Set fp back to the beginning of file
    fseek(fp, 0L, SEEK_SET);
    long file_position = 0; // position within a file

    uint32_t read_bits, write_bits, write_tmp;
    uint32_t i;

    // Set up SPI mode
    wiringPiSPISetupMode(0, 100000, 3);
    
    // Wait until GBA returns slave info
    waitSPI32(0x00006202, 0x72026202, "Looking for GBA");

    read_bits = writeSPI32(0x00006202, "Found GBA");
    read_bits = writeSPI32(0x00006102, "Recognition OK");

    fprintf(stdout, "Sending header\n");

    for (i = 0; i < 0x5f; i++) {
        write_bits = getc(fp);
        write_bits |= (getc(fp) << 8);
        file_position += 2;

        read_bits = writeSPI32NoMessage(write_bits);
    } // for
     
    read_bits = writeSPI32(0x00006200, "Transfer of header data completed");
    read_bits = writeSPI32(0x00006202, "Exchange master/slave info again");

    read_bits = writeSPI32(0x000063d1, "Send palette data");
    read_bits = writeSPI32(0x000063d1, "Send palette data, receive 0x73hh****"); 

    uint32_t m = ((read_bits & 0x00ff0000) >>  8) + 0xffff00d1; // keymul
    uint32_t hh = ((read_bits & 0x00ff0000) >> 16) + 0xf; // handshake data

    uint32_t handshake_bits = (((read_bits >> 16) + 0xf) & 0xff) | 0x00006400;

    read_bits = writeSPI32(handshake_bits, "Send handshake data");
    read_bits = writeSPI32((file_size - 0x190) / 4, "Send length info, receive seed 0x**cc****");

	uint32_t f = (((read_bits & 0x00ff0000) >> 8) + hh) | 0xffff0000; // chkfin
	uint32_t c = 0x0000c387; // chksum
    uint32_t x = 0x0000c37b; // chkxor
    uint32_t k = 0x43202f2f; // keyxor

    fprintf(stdout, "Send encrypted data\n");

    while (file_position < file_size) {
        // bits to write
        write_bits = getc(fp);
        write_bits |= (getc(fp) << 8);
        write_bits |= (getc(fp) << 16);
        write_bits |= (getc(fp) << 24);

        // temporarily store for encryption
        write_tmp = write_bits;

        for (i = 0; i < 32; i++) {
            if ((c ^ write_bits) & 0x01) { // if c xor write_bits has carry
                c = (c >> 1) ^ x;
            } else {
                c >>= 1;
            } // else
            write_bits >>= 1;
        } // for 

        m = (0x6f646573 * m) + 1;

        writeSPI32NoMessage(write_tmp ^ ((~(0x02000000 + file_position)) + 1) ^ m ^ k);

        file_position += 4;
    } // while

    fclose(fp); // Sent all file data so closing file

    for (i = 0; i < 32; i++) {
        if ((c ^ f) & 0x01) { // if c xor f has carry
            c = (c >> 1) ^ x;
        } else {
            c >>= 1;
        } // else
        f >>= 1;
    } // for

    waitSPI32(0x00000065, 0x00750065, "Wait for GBA to respond with CRC");

    read_bits = writeSPI32(0x00000066, "GBA ready with CRC");
    read_bits = writeSPI32(c, "Let's exchange CRC!");

    fprintf(stdout, "CRC ...hope they match!\n");
    fprintf(stdout, "MulitBoot done\n");

} // main
