//****************************************************************************
// UART.c
// Runs on MSP432E401Y
// Simple device driver for UART0
//
// Copyright 2014 by Jonathan W. Valvano, valvano@mail.utexas.edu
//    You may use, edit, run or distribute this file
//    as long as the above copyright notice remains
//
//  Modified to set 9600 baud rate   L.Aamodt  5/1/18
//****************************************************************************
// U0Rx (VCP receive) connected to PA0
// U0Tx (VCP transmit) connected to PA1
// Note: Connected LaunchPad JP4 and JP5 inserted parallel with long side of board.

// To use UART7 pins 24(O) and 25(I) are used
// pin 24 is PC5 and 25 is PC4


#include <stdint.h>
#include "UART.h"

#define GPIO_PORTA_AFSEL_R      (*((volatile uint32_t *)0x4005A420))
#define GPIO_PORTA_DEN_R        (*((volatile uint32_t *)0x4005A51C))
#define GPIO_PORTA_AMSEL_R      (*((volatile uint32_t *)0x4005A528))
#define GPIO_PORTA_PCTL_R       (*((volatile uint32_t *)0x4005A52C))
#define UART0_DR_R              (*((volatile uint32_t *)0x40013000))
#define UART0_FR_R              (*((volatile uint32_t *)0x40013018))
#define UART_FR_TXFF            0x00000020  // UART Transmit FIFO Full
#define UART_FR_RXFE            0x00000010  // UART Receive FIFO Empty
#define UART0_IBRD_R            (*((volatile uint32_t *)0x40013024))
#define UART0_FBRD_R            (*((volatile uint32_t *)0x40013028))
#define UART0_LCRH_R            (*((volatile uint32_t *)0x4001302C))
#define UART_LCRH_WLEN_8        0x00000060  // 8 bit word length
#define UART_LCRH_FEN           0x00000010  // UART Enable FIFOs
#define UART0_CTL_R             (*((volatile uint32_t *)0x40013030))
#define UART_CTL_HSE            0x00000020  // High-Speed Enable
#define UART_CTL_UARTEN         0x00000001  // UART Enable
#define UART0_CC_R              (*((volatile uint32_t *)0x40013FC8))
#define UART_CC_CS_M            0x0000000F  // UART Baud Clock Source
#define UART_CC_CS_SYSCLK       0x00000000  // System clock (based on clock
                                            // source and divisor factor)
#define UART_CC_CS_PIOSC        0x00000005  // PIOSC
#define SYSCTL_ALTCLKCFG_R      (*((volatile uint32_t *)0x400FE138))
#define SYSCTL_ALTCLKCFG_ALTCLK_M                                             \
                                0x0000000F  // Alternate Clock Source
#define SYSCTL_ALTCLKCFG_ALTCLK_PIOSC                                         \
                                0x00000000  // PIOSC
#define SYSCTL_RCGCGPIO_R       (*((volatile uint32_t *)0x400FE608))
#define SYSCTL_RCGCGPIO_R0      0x00000004  // GPIO Port A Run Mode Clock
                                            // Gating Control
#define SYSCTL_RCGCUART_R       (*((volatile uint32_t *)0x400FE618))
#define SYSCTL_RCGCUART_R0      0x00000080  // UART Module 0 Run Mode Clock
                                            // Gating Control
#define SYSCTL_PRGPIO_R         (*((volatile uint32_t *)0x400FEA08))
#define SYSCTL_PRGPIO_R0        0x00000004  // GPIO Port A Peripheral Ready
#define SYSCTL_PRUART_R         (*((volatile uint32_t *)0x400FEA18))
#define SYSCTL_PRUART_R0        0x00000080  // UART Module 0 Peripheral Ready

//------------UART_Init------------
// Initialize UART0 for 115,200 baud rate (clock from 16 MHz PIOSC),
// 8 bit word length, no parity bits, one stop bit, FIFOs enabled,
// no interrupts
// Input: none
// Output: none
void UART_Init(void){
                                        // activate clock for UART0
  SYSCTL_RCGCUART_R |= SYSCTL_RCGCUART_R0;
                                        // activate clock for Port A
  SYSCTL_RCGCGPIO_R |= SYSCTL_RCGCGPIO_R0;
                                        // allow time for clock to stabilize
  while((SYSCTL_PRUART_R&SYSCTL_PRUART_R0) == 0){};
  UART0_CTL_R &= ~UART_CTL_UARTEN;      // disable UART
  UART0_IBRD_R = 104;                     // IBRD = int(16,000,000 / (16 * 9600)) = int(104.1666)
  UART0_FBRD_R = 40;                    // FBRD = round(0.1666 * 64) = 11 but emperically 40 is better
                                        // 8 bit word length (no parity bits, one stop bit, FIFOs)
  UART0_LCRH_R = (UART_LCRH_WLEN_8|UART_LCRH_FEN);
                                        // UART gets its clock from the alternate clock source as defined by SYSCTL_ALTCLKCFG_R
  UART0_CC_R = (UART0_CC_R&~UART_CC_CS_M)+UART_CC_CS_PIOSC;
                                        // the alternate clock source is the PIOSC (default)
  SYSCTL_ALTCLKCFG_R = (SYSCTL_ALTCLKCFG_R&~SYSCTL_ALTCLKCFG_ALTCLK_M)+SYSCTL_ALTCLKCFG_ALTCLK_PIOSC;
  UART0_CTL_R &= ~UART_CTL_HSE;         // high-speed disable; divide clock by 16 rather than 8 (default)
  UART0_CTL_R |= UART_CTL_UARTEN;       // enable UART
                                        // allow time for clock to stabilize
  while((SYSCTL_PRGPIO_R&SYSCTL_PRGPIO_R0) == 0){};
  GPIO_PORTA_AFSEL_R |= 0x30;           // enable alt funct on PA1-0
  GPIO_PORTA_DEN_R |= 0x30;             // enable digital I/O on PA1-0
                                        // configure PA1-0 as UART
  GPIO_PORTA_PCTL_R = (GPIO_PORTA_PCTL_R&0xFF00FFFF)+0x00110000;
  GPIO_PORTA_AMSEL_R &= ~0x30;          // disable analog functionality on PA
}

//------------UART_InChar------------
// Wait for new serial port input
// Input: none
// Output: ASCII code for key typed
char UART_InChar(void){
  while((UART0_FR_R&UART_FR_RXFE) != 0);
  return((uint8_t)(UART0_DR_R&0xFF));
}

//------------UART_OutChar------------
// Output 8-bit to serial port
// Input: letter is an 8-bit ASCII character to be transferred
// Output: none
void UART_OutChar(char data){
  while((UART0_FR_R&UART_FR_TXFF) != 0);
  UART0_DR_R = data;
}

void UART_OutCommand(uint32_t data) {
	while((UART0_FR_R&UART_FR_TXFF) != 0);
  UART0_DR_R = data;
}

//------------UART_OutString------------
// Output String (NULL termination)
// Input: pointer to a NULL-terminated string to be transferred
// Output: none
void UART_OutString(char *pt){
  while(*pt){
    UART_OutChar(*pt);
    pt++;
  }
}

//------------UART_InUDec------------
// InUDec accepts ASCII input in unsigned decimal format
//     and converts to a 32-bit unsigned number
//     valid range is 0 to 4294967295 (2^32-1)
// Input: none
// Output: 32-bit unsigned number
// If you enter a number above 4294967295, it will return an incorrect value
// Backspace will remove last digit typed
uint32_t UART_InUDec(void){
uint32_t number=0, length=0;
char character;
  character = UART_InChar();
  while(character != CR){ // accepts until <enter> is typed
// The next line checks that the input is a digit, 0-9.
// If the character is not 0-9, it is ignored and not echoed
    if((character>='0') && (character<='9')) {
      number = 10*number+(character-'0');   // this line overflows if above 4294967295
      length++;
      UART_OutChar(character);
    }
// If the input is a backspace, then the return number is
// changed and a backspace is outputted to the screen
    else if((character==BS) && length){
      number /= 10;
      length--;
      UART_OutChar(character);
    }
    character = UART_InChar();
  }
  return number;
}

//-----------------------UART_OutUDec-----------------------
// Output a 32-bit number in unsigned decimal format
// Input: 32-bit number to be transferred
// Output: none
// Variable format 1-10 digits with no space before or after
void UART_OutUDec(uint32_t n){
// This function uses recursion to convert decimal number
//   of unspecified length as an ASCII string
  if(n >= 10){
    UART_OutUDec(n/10);
    n = n%10;
  }
  UART_OutChar(n+'0'); /* n is between 0 and 9 */
}

//---------------------UART_InUHex----------------------------------------
// Accepts ASCII input in unsigned hexadecimal (base 16) format
// Input: none
// Output: 32-bit unsigned number
// No '$' or '0x' need be entered, just the 1 to 8 hex digits
// It will convert lower case a-f to uppercase A-F
//     and converts to a 16 bit unsigned number
//     value range is 0 to FFFFFFFF
// If you enter a number above FFFFFFFF, it will return an incorrect value
// Backspace will remove last digit typed
uint32_t UART_InUHex(void){
uint32_t number=0, digit, length=0;
char character;
  character = UART_InChar();
  while(character != CR){
    digit = 0x10; // assume bad
    if((character>='0') && (character<='9')){
      digit = character-'0';
    }
    else if((character>='A') && (character<='F')){
      digit = (character-'A')+0xA;
    }
    else if((character>='a') && (character<='f')){
      digit = (character-'a')+0xA;
    }
// If the character is not 0-9 or A-F, it is ignored and not echoed
    if(digit <= 0xF){
      number = number*0x10+digit;
      length++;
      UART_OutChar(character);
    }
// Backspace outputted and return value changed if a backspace is inputted
    else if((character==BS) && length){
      number /= 0x10;
      length--;
      UART_OutChar(character);
    }
    character = UART_InChar();
  }
  return number;
}

//--------------------------UART_OutUHex----------------------------
// Output a 32-bit number in unsigned hexadecimal format
// Input: 32-bit number to be transferred
// Output: none
// Variable format 1 to 8 digits with no space before or after
void UART_OutUHex(uint32_t number){
// This function uses recursion to convert the number of
//   unspecified length as an ASCII string
  if(number >= 0x10){
    UART_OutUHex(number/0x10);
    UART_OutUHex(number%0x10);
  }
  else{
    if(number < 0xA){
      UART_OutChar(number+'0');
     }
    else{
      UART_OutChar((number-0x0A)+'A');
    }
  }
}

//------------UART_InString------------
// Accepts ASCII characters from the serial port
//    and adds them to a string until <enter> is typed
//    or until max length of the string is reached.
// It echoes each character as it is inputted.
// If a backspace is inputted, the string is modified
//    and the backspace is echoed
// terminates the string with a null character
// uses busy-waiting synchronization on RDRF
// Input: pointer to empty buffer, size of buffer
// Output: Null terminated string
// -- Modified by Agustinus Darmawan + Mingjie Qiu --
void UART_InString(char *bufPt, uint16_t max) {
uint16_t length=0;
char character;
  character = UART_InChar();
  while(character != CR){
    if(character == BS){
      if(length){
        bufPt--;
        length--;
        UART_OutChar(BS);
      }
    }
    else if(length < max){
      *bufPt = character;
      bufPt++;
      length++;
      UART_OutChar(character);
    }
    character = UART_InChar();
  }
  *bufPt = 0;
}
