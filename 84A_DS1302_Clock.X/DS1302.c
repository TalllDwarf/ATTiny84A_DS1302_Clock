#include "DS1302.h"

void init_ds1302(uint8_t* time)
{
    //Set pins as output
    TIME_CE_DDR |= (1 << TIME_CE);
    TIME_CLOCK_DDR |= (1 << TIME_CLOCK);
    TIME_DATA_DDR |= (1 << TIME_DATA);
    
    //Set all pins to LOW
    TIME_CE_PORT &= ~((1 << TIME_CE));
    TIME_CLOCK_PORT &= ~(1 << TIME_CLOCK);
    TIME_DATA_PORT &= ~(1 << TIME_DATA);
   
    //Disable write protection and trickle charge
    write_to_ds1302(DS1302_WRITE_PROTECTION, 0x00);
    write_to_ds1302(DS1302_TRICKLE_CHARGE, 0x00);
    
    //Read current date/time from ds1302
    //burst_read_from_ds1302(time);
    
    //Testing
    burst_write_to_ds1302(time);
}

void start_ds1302(void)
{
    //Set LOW for transmission
    TIME_CLOCK_PORT &= ~(1 << TIME_CLOCK);
    TIME_DATA_PORT &= ~(1 << TIME_DATA);
    TIME_CE_PORT &= ~(1 << TIME_CE);
    
    //Small delay then start transmission
    NOP2();    
    TIME_CE_PORT |= (1 << TIME_CE);
    NOP2();
}

void stop_ds1302(void)
{   
    //Set CE to LOW to stop transmission
    TIME_CE_PORT &= ~(1 << TIME_CE);
    
    NOP2();
    
    //Set clock and data to LOW ready for next transmission
    TIME_CLOCK_PORT &= ~(1 << TIME_CLOCK);
    TIME_DATA_PORT &= ~(1 << TIME_DATA);
}

void write_byte_to_ds1302(uint8_t data)
{
    //Set time data as output
    TIME_DATA_DDR |= (1 << TIME_DATA);
    
    for(uint8_t i = 0; i < 8; ++i)
    {
        //Clock to LOW
        TIME_CLOCK_PORT &= ~(1 << TIME_CLOCK);
        NOP2();
        
        //Set data
        if(data & (0x01 << i))
            TIME_DATA_PORT |= (1 << TIME_DATA);
        else
            TIME_DATA_PORT &= ~(1 << TIME_DATA);
        
        //Clock HIGH
        //ds1302 reads on LOW to HIGH
        NOP2();
        TIME_CLOCK_PORT |= (1 << TIME_CLOCK);
        NOP2();
        
    }
}

void write_to_ds1302(uint8_t address, uint8_t data)
{    
    start_ds1302();
    write_byte_to_ds1302(address);
    write_byte_to_ds1302(data);
    stop_ds1302();
}

void read_byte_from_ds1302(uint8_t* data)
{
    //Set time data as input
    TIME_DATA_DDR &= ~(1 << TIME_DATA);
    TIME_DATA_PORT &= ~(1 << TIME_DATA);
    NOP();
    
    //Clear data
    *data = 0x00;
    
    for(uint8_t i = 0; i <= 7; ++i)
    {
        TIME_CLOCK_PORT &= ~(1 << TIME_CLOCK);
        NOP2();
        
        TIME_CLOCK_PORT |= (1 << TIME_CLOCK);
        NOP2();
        
        if(bit_is_set(TIME_DATA_PIN, TIME_DATA))
            *data |= (1 << i);      
    }
}

void burst_read_from_ds1302(uint8_t* ds1302_data)
{
    start_ds1302();
    write_byte_to_ds1302(READ_ADDRESS(DS1302_CLOCK_BURST));
    
    for(uint8_t i = 0; i < 8; ++i)
    {
        read_byte_from_ds1302(ds1302_data);
        ds1302_data++;
    }
    
    stop_ds1302();
}

void burst_write_to_ds1302(uint8_t* time)
{
    start_ds1302();
    write_byte_to_ds1302(DS1302_CLOCK_BURST);
    
    for(uint8_t i = 0; i < 8; ++i)
    {
        write_byte_to_ds1302(*time);
        time++;
    }
    
    stop_ds1302();
}

void read_from_address_ds1302(uint8_t address, uint8_t* data)
{    
    //Set address r/w bit to read
    address = READ_ADDRESS(address);
    
    start_ds1302();
    write_byte_to_ds1302(address);
    read_byte_from_ds1302(data);
    stop_ds1302();
}