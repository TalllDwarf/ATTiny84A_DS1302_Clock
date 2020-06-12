/*
 * File:   newavr-main.c
 * Author: TallDwarf
 *
 * Created on 27 May 2020, 19:04
 */

#include <avr/io.h>
#include <avr/interrupt.h>

#define TIME_CE PORTA7
#define TIME_CE_PORT PORTA
#define TIME_CLOCK PORTB0
#define TIME_CLOCK_PORT PORTB
#define TIME_DATA PORTB1
#define TIME_DATA_PORT PORTB

#include "DS1302.h"

//TIMER prescalers 
#define N_1(TIMER) (1 << CS ## TIMER ## 0)
#define N_8(TIMER) (1 << CS ## TIMER ## 1)
#define N_64(TIMER) (1 << CS ## TIMER ## 1 | 1 << CS ## TIMER ## 0)
#define N_256(TIMER) (1 << CS ## TIMER ## 2)
#define N_1024(TIMER) (1 << CS ## TIMER ## 2 | 1 << CS ## TIMER ## 0)

#define DIGIT_DATA PORTA0
#define DIGIT_CLOCK PORTA1
#define DIGIT_LATCH PORTA2
#define DIGIT_CLEAR PORTA4
#define DIGIT_OUTPUT PORTB2

#define LEFT_BUTTON PINA5
#define LEFT_BUTTON_PIN PINA
#define CENTER_BUTTON PINB3
#define CENTER_BUTTON_PIN PINB
#define RIGHT_BUTTON PINA6
#define RIGHT_BUTTON_PIN PINA

#define BRIGHTNESS_PIN PORTA3

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

#define LOW 0
#define HIGH 1

#define TRUE 1
#define FALSE 0

//Number of timer ticks to generate a second
#define ONE_SECOND_MULTIPLE 2

//5 Second delay
#define MENU_TIMEOUT_MAX ONE_SECOND_MULTIPLE * 5

//Pin held high and buttons pulls it low
#define PRESSED(Old_state, New_state) (Old_state == HIGH && New_state == LOW)
#define RELEASED(Old_state, New_state) (Old_state == LOW && New_state == HIGH)

typedef union
{
    struct 
    {
        uint8_t edit_Minutes : 1;
        uint8_t edit_Hours : 1;
        uint8_t edit_12_24 : 1;
        uint8_t edit_Date : 1;
        uint8_t edit_Month : 1; 
        uint8_t edit_Weekday : 1;
        uint8_t edit_Year : 1;
        uint8_t selecting : 1;
    } Menu_Data;
    //If one edit is selected menu is enabled
    struct
    {
        uint8_t enabled : 7;
        uint8_t setting : 1;
    }Menu_State;
} Menu;

typedef struct
{
    uint8_t Left_Button_Stat : 1;
    uint8_t Left_Button_Old : 1;
    uint8_t Right_Button_Stat : 1;
    uint8_t Right_Button_Old : 1;
    uint8_t Center_Button_Stat : 1;
    uint8_t Center_Button_Old : 1;
    uint8_t Reserved : 2;
} ButtonState;


typedef struct 
{
    uint8_t Led : 1;
    uint8_t Save : 1;
    uint8_t FlipFlop : 1;
    uint8_t Time : 5;    
} Pending;

//Time data set
DS1302_DATA_SET time_ds1302 = 
{
    .seconds = 0,
    .secondsX10 = 0,
    .clockHalt = 0,
    .minutes = 0,
    .minutesX10 = 3,
    .H12.hour = 5,
    .H12.hourX10 = 0,
    .H12.hour_AM_PM = 1,
    .H12.hour_12_24 = 1,
    .date = 1,
    .dateX10 = 1,
    .month = 6,
    .monthX10 = 0,
    .day = 1,
    .year = 0,
    .yearX10 = 2,
    .writeProtection = 0,
    .trickleCharger = 0
};

Menu menu = {0};
volatile uint8_t menuTimeout = 0;

ButtonState buttons = {0};
volatile Pending pending;

const uint8_t segmentNumbers[10] = 
{
    0b11111100, //0
    0b01100000, //1
    0b11011010, //2
    0b11110010, //3
    0b01100110, //4
    0b10110110, //5
    0b10111110, //6
    0b11100000, //7
    0b11111110, //8
    0b11100110  //9
};

uint8_t digits[4] = 
{
    0b11111100, 0b11111100, 0b11111100, 0b11111100
};

///////////////////////
//PWM
//////////////////////

void init_pwm(void)
{
    //B2 to output
    DDRB |= (1 << PORTB2);    
    
    //start with 50% duty
    OCR0A = 128;
    
    //Fast PWM
    TCCR0A |= (1 << WGM00) | (1 << WGM01);
    
    //Non inverting
    TCCR0A |= (1 << COM0A1);
    
    //Prescaler to 64
    TCCR0B |= N_64(0);    
}

inline void set_pwm_duty(uint8_t duty)
{
    OCR0A = duty;
}

///////////////////////
//ADC
//////////////////////

inline void init_adc(void)
{
    DDRA &= ~(1 << PORTA3);
    ADMUX |= (1 << MUX0) | (1 << MUX1); // PA3 as ADC input
    
    // Enable ADC - Do not start ADC - Enable Auto Trigger - Clear Interrupt Flag - Disable Interrupt - prescaler to 128
    ADCSRA = 0b10000111;
    ADCSRB |= (1 << ADLAR);
    
}

inline void start_adc(void)
{
    ADCSRA |= (1 << ADSC);
}

uint8_t read_ADC(void)
{    
    //Wait for ADC to finish
    loop_until_bit_is_clear(ADCSRA, ADSC);

    return MAX(50, ADCH);
}

//////////////////////////
//LED Render
//////////////////////////

//Shift out all digits
void write_digits(void)
{
    PORTA &= ~((1 << DIGIT_DATA) | (1 << DIGIT_CLOCK));
    
    for(uint8_t digitIndex = 0; digitIndex < 4; ++digitIndex)
    {
        uint8_t data = digits[3 - digitIndex];
        for(uint8_t i = 0; i < 8; ++i)
        {
            PORTA &= ~(1 << DIGIT_CLOCK);          
            
            if(data & 0x01)
                PORTA |= (1 << DIGIT_DATA);                
            else
                PORTA &= ~(1 << DIGIT_DATA);
            
            NOP2();
            PORTA |= (1 << DIGIT_CLOCK);
            NOP2();
            
            data = data >> 1;
        }
    }
    
    PORTA &= ~(1 << DIGIT_CLOCK);  
    PORTA &= ~(1 << DIGIT_DATA);
}

//Checks if the leds need updating if they do update segment displays
void render(void)
{
    if(pending.Led)
    {
        //Write to 595        
        PORTA |= (1 << DIGIT_LATCH);                    
        write_digits();        
        PORTA &= ~(1 << DIGIT_LATCH);  
        pending.Led = 0;
    }
}

void set_clock_digits(void)
{
    //If the menu is open
    if(menu.Menu_State.enabled)
    {
        //Only show minutes and slow flash
        if(menu.Menu_Data.edit_Minutes)
        {
            digits[0] = 0x00;
            digits[1] = 0x00;
            
            
            if(pending.FlipFlop || menu.Menu_State.setting)
            {
                digits[2] = segmentNumbers[MIN(9, time_ds1302.minutesX10)];
                digits[3] = segmentNumbers[MIN(9, time_ds1302.minutes)];
            }
            else
            {
                digits[2] = 0x00;
                digits[3] = 0x00;
            }
        }
        else if(menu.Menu_Data.edit_Hours)
        {
            if(pending.FlipFlop || menu.Menu_State.setting)
            {
                if(IS_24_HOUR(time_ds1302))
                {
                    digits[0] = segmentNumbers[MIN(9, time_ds1302.H24.hourX10)];
                    digits[1] = segmentNumbers[MIN(9, time_ds1302.H24.hour)];
                }
                else
                {    
                    digits[0] = segmentNumbers[MIN(9, time_ds1302.H12.hourX10)];
                    digits[1] = segmentNumbers[MIN(9, time_ds1302.H12.hour)];
                }
            }
            else
            {
                digits[0] = 0x00;
                digits[1] = 0x00;
            }
            
            digits[2] = 0x00;
            digits[3] = 0x00;
        }
        else if(menu.Menu_Data.edit_12_24)
        {
            if(IS_24_HOUR(time_ds1302))
            {
                digits[0] = 0x00;
                digits[1] = 0x00;
                
                if(pending.FlipFlop || menu.Menu_State.setting)
                {
                    digits[2] = segmentNumbers[2];
                    digits[3] = segmentNumbers[4];
                }
                else
                {
                    digits[2] = 0x00;
                    digits[3] = 0x00;
                }                       
            }
            else
            {
                if(pending.FlipFlop || menu.Menu_State.setting)
                {
                    digits[0] = segmentNumbers[1];
                    digits[1] = segmentNumbers[2];
                }
                else
                {
                    digits[0] = 0x00;
                    digits[1] = 0x00;
                }
                
                digits[2] = 0x00;
                digits[3] = 0x00;
            }
        }
        else if(menu.Menu_Data.edit_Weekday)
        {
            digits[0] = 0x00;
            digits[1] = 0x00;
            digits[2] = 0x00;
            
            if(pending.FlipFlop || menu.Menu_State.setting)
            {
                digits[3] = segmentNumbers[MIN(7, time_ds1302.day)];
            }
            else
            {
                digits[3] = 0x00;
            }   
        }
        else if(menu.Menu_Data.edit_Date)
        {
            if(pending.FlipFlop || menu.Menu_State.setting)
            {
                digits[0] = segmentNumbers[MIN(3, time_ds1302.dateX10)];
                digits[1] = segmentNumbers[MIN(9, time_ds1302.date)];
            }
            else
            {
                digits[0] = 0x00;
                digits[1] = 0x00;
            }
            
            digits[2] = 0x00;
            digits[3] = 0x00;
        }
        else if(menu.Menu_Data.edit_Month)
        {
            digits[0] = 0x00;
            digits[1] = 0x00;
            
            if(pending.FlipFlop || menu.Menu_State.setting)
            {
                digits[2] = segmentNumbers[MIN(1, time_ds1302.monthX10)];
                digits[3] = segmentNumbers[MIN(9, time_ds1302.month)];
            }
            else
            {
                digits[2] = 0x00;
                digits[3] = 0x00;
            }
        }
        else if(menu.Menu_Data.edit_Year)
        {
            if(pending.FlipFlop || menu.Menu_State.setting)
            {
                digits[0] = segmentNumbers[2];
                digits[1] = segmentNumbers[0];
                digits[2] = segmentNumbers[MIN(9, time_ds1302.yearX10)];
                digits[3] = segmentNumbers[MIN(9, time_ds1302.year)];
            }
            else
            {
                digits[0] = 0x00;
                digits[1] = 0x00;
                digits[2] = 0x00;
                digits[3] = 0x00;
            }            
        }
        
        pending.Led = TRUE;        
    }
    //else show time
    else
    {    
        if(IS_24_HOUR(time_ds1302))
        {
            digits[0] = segmentNumbers[MIN(2, time_ds1302.H24.hourX10)];
            digits[1] = segmentNumbers[MIN(9, time_ds1302.H24.hour)];
        }
        else
        {    
            digits[0] = segmentNumbers[MIN(1, time_ds1302.H12.hourX10)];
            digits[1] = segmentNumbers[MIN(9, time_ds1302.H12.hour)];
        }

            digits[2] = segmentNumbers[MIN(5, time_ds1302.minutesX10)];
            digits[3] = segmentNumbers[MIN(9, time_ds1302.minutes)];
    }
}

inline void save_time()
{
    //Start clock
    write_to_ds1302(DS1302_SECOND, 0x00);
    
    //Write new data
    burst_write_to_ds1302((uint8_t*)&time_ds1302);
}

///////////////////////
//Timer 1 - 0.5 second delay
//////////////////////

inline void init_timer1(void)
{    
    OCR1AH = 0xF4; 
    OCR1AL = 0x24;
    TCCR1A = 0x80;
    TCCR1B |= ((1 << WGM12) | N_64(1)); 
    TIMSK1 |= (1 << OCIE1A);
}

ISR(TIM1_COMPA_vect)
{    
    if(menuTimeout > 0)
        --menuTimeout;
    
    pending.Time = TRUE;
    pending.FlipFlop = ~pending.FlipFlop;
}

void update_time()
{
    //Every .5 seconds update time with the DS1302 to make sure we are on track
    if(menu.Menu_State.enabled == FALSE && pending.Time)
    {            
        pending.Led = TRUE;

        uint8_t* time_ptr = (uint8_t*)&time_ds1302;
        
        //Read only seconds
        read_from_address_ds1302(DS1302_SECOND, time_ptr);

        //If seconds have reset read all data
        if(*time_ptr == 0x00)
        {
            burst_read_from_ds1302(time_ptr);
        }
        pending.Time = FALSE;  
    }        

    set_clock_digits();
}

//Store last input value and get new value
//Debounce done on PCB
void update_input(void)
{
    buttons.Center_Button_Old = buttons.Center_Button_Stat;    
    buttons.Center_Button_Stat = (CENTER_BUTTON_PIN & (1 << CENTER_BUTTON)) ? 1 : 0;
    
    buttons.Left_Button_Old = buttons.Left_Button_Stat;
    buttons.Left_Button_Stat = (LEFT_BUTTON_PIN & (1 << LEFT_BUTTON)) ? 1 : 0;
    
    buttons.Right_Button_Old = buttons.Right_Button_Stat;
    buttons.Right_Button_Stat = (RIGHT_BUTTON_PIN & (1 << RIGHT_BUTTON)) ? 1 : 0;
}

void update_menu(void)
{
    //If the menu is not currently enabled
    if(menu.Menu_State.enabled == FALSE)
    {
        //Check for any button press to open menu
        if(PRESSED(buttons.Center_Button_Old, buttons.Center_Button_Stat) ||
                PRESSED(buttons.Left_Button_Old, buttons.Left_Button_Stat) || 
                PRESSED(buttons.Right_Button_Old, buttons.Right_Button_Stat))
        {
            menu.Menu_State.enabled = 0x00;
            menu.Menu_State.setting = 0x00;
            menu.Menu_Data.edit_Minutes = TRUE;
            
            menuTimeout = MENU_TIMEOUT_MAX;
            
            //Stop clock
            write_to_ds1302(DS1302_SECOND, 0x80);
        }
        
        return;
    }
    else
    {
        if(menuTimeout == 0)
        {
            menu.Menu_State.enabled = 0x00;
            menu.Menu_Data.selecting = FALSE;
            save_time();
            return;
        }
    }
    
    if(PRESSED(buttons.Right_Button_Old, buttons.Right_Button_Stat))
    {
        if(menu.Menu_Data.selecting == FALSE)
        {
            menu.Menu_Data.selecting = TRUE;
            menuTimeout = MENU_TIMEOUT_MAX;
        }
        else
        {
            menu.Menu_State.enabled = 0x00;
            menu.Menu_Data.selecting = FALSE;
            menuTimeout = 0;
            save_time();
        }
    }
    
    if(menu.Menu_Data.edit_Minutes)
    {
        if(menu.Menu_Data.selecting == FALSE)
        {
            if(PRESSED(buttons.Left_Button_Old, buttons.Left_Button_Stat))
            {
                menu.Menu_State.enabled = 0x00;
                menu.Menu_Data.edit_Year = 1;
                menuTimeout = MENU_TIMEOUT_MAX;
            }
//            else if(PRESSED(buttons.Right_Button_Old, buttons.Right_Button_Stat))
//            {
//                menu.Menu_State.enabled = 0x00;
//                menu.Menu_Data.edit_Hours = 1;
//                menuTimeout = MENU_TIMEOUT_MAX;
//            }
        }
        else
        {
            if(PRESSED(buttons.Left_Button_Old, buttons.Left_Button_Stat))
            {
                uint8_t mins = COMBINE(time_ds1302.minutesX10, time_ds1302.minutes);
                
                if(mins == 0)
                    mins = 59;
                else
                    mins--;
                
                time_ds1302.minutesX10 = GET_X10(mins);
                time_ds1302.minutes = GET_X1(mins);
                menuTimeout = MENU_TIMEOUT_MAX;
            }
//            else if(PRESSED(buttons.Right_Button_Old, buttons.Right_Button_Stat))
//            {
//                uint8_t mins = COMBINE(time_ds1302.minutesX10, time_ds1302.minutes);
//                
//                if(mins == 59)
//                    mins = 0;
//                else
//                    mins++;
//                
//                time_ds1302.minutesX10 = GET_X10(mins);
//                time_ds1302.minutes = GET_X1(mins);
//                menuTimeout = MENU_TIMEOUT_MAX;
//            }
        }
    }
    else if(menu.Menu_Data.edit_Hours)
    {
        if(menu.Menu_Data.selecting == FALSE)
        {
            if(PRESSED(buttons.Left_Button_Old, buttons.Left_Button_Stat))
            {
                menu.Menu_State.enabled = 0x00;
                menu.Menu_Data.edit_Minutes = TRUE;
                menuTimeout = MENU_TIMEOUT_MAX;
            }
//            else if(PRESSED(buttons.Right_Button_Old, buttons.Right_Button_Stat))
//            {
//                menu.Menu_State.enabled = 0x00;
//                menu.Menu_Data.edit_12_24 = TRUE;
//                menuTimeout = MENU_TIMEOUT_MAX;
//            }
        }
        else
        {      
            if(IS_24_HOUR(time_ds1302))
            {            
                if(PRESSED(buttons.Left_Button_Old, buttons.Left_Button_Stat))
                {
                    uint8_t hour = COMBINE(time_ds1302.H24.hourX10, time_ds1302.H24.hour);
                    
                    if(hour == 0)
                    {
                        hour = 23;
                    }
                    else
                    {
                        hour--;
                    }
                    
                    time_ds1302.H24.hourX10 = GET_X10(hour);
                    time_ds1302.H24.hour = GET_X1(hour);
                    menuTimeout = MENU_TIMEOUT_MAX;
                }
//                else if(PRESSED(buttons.Right_Button_Old, buttons.Right_Button_Stat))
//                {
//                     uint8_t hour = COMBINE(time_ds1302.H24.hourX10, time_ds1302.H24.hour);
//                    
//                    if(hour == 23)
//                    {
//                        hour = 0;
//                    }
//                    else
//                    {
//                        hour++;
//                    }
//                    
//                    time_ds1302.H24.hourX10 = GET_X10(hour);
//                    time_ds1302.H24.hour = GET_X1(hour);
//                    menuTimeout = MENU_TIMEOUT_MAX;
//                }
            }
            else
            {            
                if(PRESSED(buttons.Left_Button_Old, buttons.Left_Button_Stat))
                {
                    uint8_t hour = COMBINE(time_ds1302.H12.hourX10, time_ds1302.H12.hour);
                    
                    if(hour == 1)
                    {
                        hour = 12;
                    }
                    else
                    {
                        hour--;
                    }
                    
                    time_ds1302.H24.hourX10 = GET_X10(hour);
                    time_ds1302.H24.hour = GET_X1(hour);
                    menuTimeout = MENU_TIMEOUT_MAX;
                }
//                else if(PRESSED(buttons.Right_Button_Old, buttons.Right_Button_Stat))
//                {
//                    uint8_t hour = COMBINE(time_ds1302.H24.hourX10, time_ds1302.H24.hour);
//                    
//                    if(hour == 12)
//                    {
//                        hour = 1;
//                    }
//                    else
//                    {
//                        hour++;
//                    }
//                    
//                    time_ds1302.H24.hourX10 = GET_X10(hour);
//                    time_ds1302.H24.hour = GET_X1(hour);
//                    menuTimeout = MENU_TIMEOUT_MAX;
//                }
            }
        }
    }
    else if(menu.Menu_Data.edit_12_24)
    {
        if(menu.Menu_Data.selecting == FALSE)
        {
            if(PRESSED(buttons.Left_Button_Old, buttons.Left_Button_Stat))
            {
                menu.Menu_State.enabled = 0x00;
                menu.Menu_Data.edit_Hours = TRUE;
                menuTimeout = MENU_TIMEOUT_MAX;
            }
//            else if(PRESSED(buttons.Right_Button_Old, buttons.Right_Button_Stat))
//            {
//                menu.Menu_State.enabled = 0x00;
//                menu.Menu_Data.edit_Weekday = TRUE;
//                menuTimeout = MENU_TIMEOUT_MAX;
//            }
        }
        else
        {
            if(PRESSED(buttons.Left_Button_Old, buttons.Left_Button_Stat) ||
                    PRESSED(buttons.Right_Button_Old, buttons.Right_Button_Stat))
            {
                time_ds1302.H24.hour_12_24 = ~time_ds1302.H24.hour_12_24;
                menuTimeout = MENU_TIMEOUT_MAX;
            }            
        }
    }
    else if(menu.Menu_Data.edit_Weekday)
    {
        if(menu.Menu_Data.selecting == FALSE)
        {
            if(PRESSED(buttons.Left_Button_Old, buttons.Left_Button_Stat))
            {
                menu.Menu_State.enabled = 0x00;
                menu.Menu_Data.edit_12_24 = TRUE;
                menuTimeout = MENU_TIMEOUT_MAX;
            }
//            else if(PRESSED(buttons.Right_Button_Old, buttons.Right_Button_Stat))
//            {
//                menu.Menu_State.enabled = 0x00;
//                menu.Menu_Data.edit_Date = TRUE;
//                menuTimeout = MENU_TIMEOUT_MAX;
//            }
        }
        else
        {
            if(PRESSED(buttons.Left_Button_Old, buttons.Left_Button_Stat))
            {
                if(time_ds1302.day == 1)
                    time_ds1302.day = 7;
                else
                    time_ds1302.day--;
                
                menuTimeout = MENU_TIMEOUT_MAX;
            }
//            else if(PRESSED(buttons.Right_Button_Old, buttons.Right_Button_Stat))
//            {
//                if(time_ds1302.day == 7)
//                    time_ds1302.day = 1;
//                else
//                    time_ds1302.day++;
//                
//                menuTimeout = MENU_TIMEOUT_MAX;
//            }
        }
    }
    else if(menu.Menu_Data.edit_Date)
    {
        if(menu.Menu_Data.selecting == FALSE)
        {
            if(PRESSED(buttons.Left_Button_Old, buttons.Left_Button_Stat))
            {
                menu.Menu_State.enabled = 0x00;
                menu.Menu_Data.edit_Weekday = TRUE;
                menuTimeout = MENU_TIMEOUT_MAX;
            }
//            else if(PRESSED(buttons.Right_Button_Old, buttons.Right_Button_Stat))
//            {
//                menu.Menu_State.enabled = 0x00;
//                menu.Menu_Data.edit_Month = TRUE;
//                menuTimeout = MENU_TIMEOUT_MAX;
//            }
        }
        else
        {
            if(PRESSED(buttons.Left_Button_Old, buttons.Left_Button_Stat))
            {
                uint8_t date = COMBINE(time_ds1302.dateX10, time_ds1302.date);
                
                if(date == 1)
                    date = 31;
                else 
                    date--;
                
                time_ds1302.dateX10 = GET_X10(date);
                time_ds1302.date = GET_X1(date);
                menuTimeout = MENU_TIMEOUT_MAX;
            }
//            else if(PRESSED(buttons.Right_Button_Old, buttons.Right_Button_Stat))
//            {
//                uint8_t date = COMBINE(time_ds1302.dateX10, time_ds1302.date);
//                
//                if(date == 31)
//                    date = 1;
//                else
//                    date++;
//                
//                time_ds1302.dateX10 = GET_X10(date);
//                time_ds1302.date = GET_X1(date);
//                menuTimeout = MENU_TIMEOUT_MAX;
//            }
        }
    }
    else if(menu.Menu_Data.edit_Month)
    {
        if(menu.Menu_Data.selecting == FALSE)
        {
            if(PRESSED(buttons.Left_Button_Old, buttons.Left_Button_Stat))
            {
                menu.Menu_State.enabled = 0x00;
                menu.Menu_Data.edit_Date = TRUE;
                menuTimeout = MENU_TIMEOUT_MAX;
            }
//            else if(PRESSED(buttons.Right_Button_Old, buttons.Right_Button_Stat))
//            {
//                menu.Menu_State.enabled = 0x00;
//                menu.Menu_Data.edit_Year = TRUE;
//                menuTimeout = MENU_TIMEOUT_MAX;
//            }
        }
        else
        {
            if(PRESSED(buttons.Left_Button_Old, buttons.Left_Button_Stat))
            {
                uint8_t month = COMBINE(time_ds1302.monthX10, time_ds1302.month);
                
                if(month == 1)
                    month = 12;
                else
                    month--;
                
                time_ds1302.monthX10 = GET_X10(month);
                time_ds1302.month = GET_X1(month);
                menuTimeout = MENU_TIMEOUT_MAX;
            }
//            else if(PRESSED(buttons.Right_Button_Old, buttons.Right_Button_Stat))
//            {
//                uint8_t month = COMBINE(time_ds1302.monthX10, time_ds1302.month);
//                
//                if(month == 12)
//                    month = 1;
//                else
//                    month++;
//                
//                time_ds1302.monthX10 = GET_X10(month);
//                time_ds1302.month = GET_X1(month);
//                menuTimeout = MENU_TIMEOUT_MAX;
//            }
        }
    }
    else if(menu.Menu_Data.edit_Year)
    {
        if(menu.Menu_Data.selecting == FALSE)
        {
            if(PRESSED(buttons.Left_Button_Old, buttons.Left_Button_Stat))
            {
                menu.Menu_State.enabled = 0x00;
                menu.Menu_Data.edit_Month = TRUE;
                menuTimeout = MENU_TIMEOUT_MAX;
            }
//            else if(PRESSED(buttons.Right_Button_Old, buttons.Right_Button_Stat))
//            {
//                menu.Menu_State.enabled = 0x00;
//                menu.Menu_Data.edit_Minutes = TRUE;
//                menuTimeout = MENU_TIMEOUT_MAX;
//            }
        }
        else
        {
            if(PRESSED(buttons.Left_Button_Old, buttons.Left_Button_Stat))
            {
                uint8_t year = COMBINE(time_ds1302.yearX10, time_ds1302.year);
                
                if(year == 0)
                    year = 99;
                else
                    year--;
                
                time_ds1302.yearX10 = GET_X10(year);
                time_ds1302.year = GET_X1(year);
                menuTimeout = MENU_TIMEOUT_MAX;
                
            }
//            else if(PRESSED(buttons.Right_Button_Old, buttons.Right_Button_Stat))
//            {
//                uint8_t year = COMBINE(time_ds1302.yearX10, time_ds1302.year);
//                
//                if(year == 99)
//                    year = 0;
//                else
//                    year++;
//                
//                time_ds1302.yearX10 = GET_X10(year);
//                time_ds1302.year = GET_X1(year);
//                menuTimeout = MENU_TIMEOUT_MAX;
//            }
        }
    }
}

int main(void) {
    
    DDRA = 0b10010111;
    DDRB = 0b00001111;
    
    PORTA &= ~((1 << DIGIT_DATA) | (1 << DIGIT_LATCH) | (1 << DIGIT_CLOCK));
    PORTA |= (1 << DIGIT_CLEAR);
    
    //Initialisation
    init_adc();
    init_pwm();
    init_timer1();
    init_ds1302((uint8_t*)&time_ds1302);

    set_clock_digits();
    
    sei();

    while (1) 
    {      
        start_adc();       
       
        update_input();
        
        update_menu();
        
        update_time();        
        
        render();
        
        set_pwm_duty(read_ADC());     
    }
}