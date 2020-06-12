/* 
 * File:   DS1302.h
 * Author: TallDwarf
 *
 * Created on 02 October 2019, 11:56
 */

#ifndef DS1302_H
#define	DS1302_H

#include <avr/io.h>

#ifndef TIME_CE
#define TIME_CE PORTA7
#endif

#ifndef TIME_CE_PORT
#define TIME_CE_PORT PORTA
#endif

#ifndef TIME_CE_DDR
#define TIME_CE_DDR DDRA
#endif

#ifndef TIME_CLOCK
#define TIME_CLOCK PORTB0
#endif

#ifndef TIME_CLOCK_PORT
#define TIME_CLOCK_PORT PORTB
#endif

#ifndef TIME_CLOCK_DDR
#define TIME_CLOCK_DDR DDRB
#endif

#ifndef TIME_DATA
#define TIME_DATA PORTB1
#endif

#ifndef TIME_DATA_PORT
#define TIME_DATA_PORT PORTB
#endif

#ifndef TIME_DATA_DDR
#define TIME_DATA_DDR DDRB
#endif

#ifndef TIME_DATA_PIN
#define TIME_DATA_PIN PINB
#endif

#define NOP() asm("nop")
#define NOP2() NOP(); NOP()
#define NOP5() NOP2(); NOP2(); NOP()

//Default values are write values
//Convert to read value using
//Value |= (1 << DS1302_READBIT);
#define DS1302_SECOND 0x80
#define DS1302_MINUTE 0x82
#define DS1302_HOUR 0x84
#define DS1302_DATE 0x86
#define DS1302_MONTH 0x88
#define DS1302_DAY 0x8A
#define DS1302_YEAR 0x8C
#define DS1302_WRITE_PROTECTION 0x8E
#define DS1302_TRICKLE_CHARGE 0x90

#define DS1302_CLOCK_BURST 0xBE

//Starting point each other point is 0xC0 + (N * 2)
//0 - 30
#define DS1302_RAM_START 0xC0
#define DS1302_RAM_END 0xFC

#define DS1302_RAM_BURST 0xFE

#define DS1302_READBIT 0

#define READ_ADDRESS(address) (address | (1 << DS1302_READBIT))

#define GET_X10(h) ((h) / 10)
#define GET_X1(l) ((l) % 10)
#define COMBINE(h, l) ((h * 10) + l)
#define STORE_COMBINE(h, l) (l | (h << 4))
#define HOUR_24_COMBINE(h, l, t) (l | (h << 4))
#define HOUR_12_COMBINE(h, l, ampm) (l | (h << 4) | (ampm << 5) | (1 << 7))

#define IS_24_HOUR(time) (time.H24.hour_12_24 == 0)  

typedef struct {
    unsigned char seconds : 4;
    unsigned char secondsX10 : 3;
    
    //1 = stop clock and go into low power mode
    //0 = on
    unsigned char clockHalt : 1;
    
    unsigned char minutes : 4;
    unsigned char minutesX10 : 3;
      
    unsigned char reserved : 1;
    
    union
    {
        struct 
        {
            unsigned char hour : 4;
            unsigned char hourX10 : 2;
            unsigned char reserved1 : 1;
            unsigned char hour_12_24 : 1; // LOW
        } H24;
        struct
        {
            unsigned char hour : 4;
            unsigned char hourX10 : 1;
            unsigned char hour_AM_PM : 1;
            unsigned char reserved1 : 1;
            unsigned char hour_12_24 : 1; // HIGH
        } H12;
    };
    
    unsigned char date : 4;
    unsigned char dateX10 : 2;
    unsigned char reserved2 : 2;
    
    unsigned char month : 4;
    unsigned char monthX10 : 1;
    unsigned char reserved3 : 3;
    
    unsigned char day : 3;
    unsigned char reserved4 : 5;
    
    unsigned char year : 4;
    unsigned char yearX10 : 4;
    
    unsigned char reserved5 : 7;
    unsigned char writeProtection : 1;
    
    unsigned char trickleCharger : 8;
} DS1302_DATA_SET;

void init_ds1302(uint8_t* time);

void start_ds1302(void);
void stop_ds1302(void);

void write_byte_to_ds1302(uint8_t data);
void write_to_ds1302(uint8_t address, uint8_t data);
void read_byte_from_ds1302(uint8_t* data);
void read_from_address_ds1302(uint8_t address, uint8_t* data);

//Read/Write all data to ds1302
void burst_read_from_ds1302(uint8_t* ds1302_data);
void burst_write_to_ds1302(uint8_t* ds1302_data);

#endif	/* DS1302_H */