/******************************************************************************
 * Timer Output Compare Demo
 *
 * Description:
 *
 * This demo configures the timer to a rate of 1 MHz, and the Output Compare
 * Channel 1 to toggle PORT T, Bit 1 at rate of 10 Hz. 
 *
 * The toggling of the PORT T, Bit 1 output is done via the Compare Result Output
 * Action bits.  
 * 
 * The Output Compare Channel 1 Interrupt is used to refresh the Timer Compare
 * value at each interrupt
 * 
 * Author:
 *  Jon Szymaniak (08/14/2009)
 *  Tom Bullinger (09/07/2011)	Added terminal framework
 *
 *****************************************************************************/


// system includes
#include <hidef.h>      /* common defines and macros */
#include <stdio.h>      /* Standard I/O Library */

// project includes
#include "types.h"
#include "derivative.h" /* derivative-specific definitions */

#include "servos.h"

// Change this value to change the frequency of the output compare signal.
// The value is in Hz.
#define OC_FREQ_HZ    ((UINT16)10)

// Macro definitions for determining the TC1 value for the desired frequency
// in Hz (OC_FREQ_HZ). The formula is:
//
// TC1_VAL = ((Bus Clock Frequency / Prescaler value) / 2) / Desired Freq in Hz
//
// Where:
//        Bus Clock Frequency     = 2 MHz
//        Prescaler Value         = 2 (Effectively giving us a 1 MHz timer)
//        2 --> Since we want to toggle the output at half of the period
//        Desired Frequency in Hz = The value you put in OC_FREQ_HZ
//
#define BUS_CLK_FREQ  ((UINT32) 2000000)   
#define PRESCALE      ((UINT16)  2)         
#define TC1_VAL       ((UINT16)  (((BUS_CLK_FREQ / PRESCALE) / 2) / OC_FREQ_HZ))


#define LED_SERVO 0
// Global reference to servo structs
Servo servos[2];


// Initializes SCI0 for 8N1, 9600 baud, polled I/O
// The value for the baud selection registers is determined
// using the formula:
//
// SCI0 Baud Rate = ( 2 MHz Bus Clock ) / ( 16 * SCI0BD[12:0] )
//--------------------------------------------------------------
void InitializeSerialPort(void)
{
    // Set baud rate to ~9600 (See above formula)
    SCI0BD = 13;          
    
    // 8N1 is default, so we don't have to touch SCI0CR1.
    // Enable the transmitter and receiver.
    SCI0CR2_TE = 1;
    SCI0CR2_RE = 1;
}


// Initializes I/O and timer settings for the demo.
//--------------------------------------------------------------       
void InitializeTimer(void)
{
  // Set the timer prescaler to %2, since the bus clock is at 2 MHz,
  // and we want the timer running at 1 MHz
  TSCR2_PR0 = 1;
  TSCR2_PR1 = 0;
  TSCR2_PR2 = 0;
    
  // Enable output compare on Channel 1
  TIOS_IOS1 = 1;
  
  // Set up output compare action to toggle Port T, bit 1
  TCTL2_OM1 = 0;
  TCTL2_OL1 = 1;
  
  // Set up timer compare value
  TC1 = TC1_VAL;
  
  // Clear the Output Compare Interrupt Flag (Channel 1) 
  TFLG1 = TFLG1_C1F_MASK;
  
  // Enable the output compare interrupt on Channel 1;
  TIE_C1I = 1;  
  
  //
  // Enable the timer
  // 
  TSCR1_TEN = 1;
    
  //
  // Enable interrupts via macro provided by hidef.h
  //
  EnableInterrupts;
}

// Output Compare Channel 1 Interrupt Service Routine
// Refreshes TC1 and clears the interrupt flag.
//          
// The first CODE_SEG pragma is needed to ensure that the ISR
// is placed in non-banked memory. The following CODE_SEG
// pragma returns to the default scheme. This is neccessary
// when non-ISR code follows. 
//
// The TRAP_PROC tells the compiler to implement an
// interrupt funcion. Alternitively, one could use
// the __interrupt keyword instead.
// 
// The following line must be added to the Project.prm
// file in order for this ISR to be placed in the correct
// location:
//		VECTOR ADDRESS 0xFFEC OC1_isr 
#pragma push
#pragma CODE_SEG __SHORT_SEG NON_BANKED
//--------------------------------------------------------------       
void interrupt 9 OC1_isr( void )
{
  TC1     +=  TC1_VAL;
  if(servos[0].wait > 0) {
    servos[0].wait--;
  }
  
  if(servos[1].wait > 0) {
    servos[1].wait--;
  }
  nextOp();
  TFLG1   =   TFLG1_C1F_MASK;
}
#pragma pop


// This function is called by printf in order to
// output data. Our implementation will use polled
// serial I/O on SCI0 to output the character.
//
// Remember to call InitializeSerialPort() before using printf!
//
// Parameters: character to output
//--------------------------------------------------------------       
void TERMIO_PutChar(INT8 ch)
{
    // Poll for the last transmit to be complete
    do
    {
      // Nothing  
    } while (SCI0SR1_TC == 0);
    
    // write the data to the output shift register
    SCI0DRL = ch;
}


// Polls for a character on the serial port.
//
// Returns: Received character
//--------------------------------------------------------------       
UINT8 GetChar(void)
{ 
  // Poll for data
  do
  {
    // Nothing
  } while(SCI0SR1_RDRF == 0);
   
  // Fetch and return data from SCI0
  return SCI0DRL;
}

// Preset numbers for setting Duty Period to position
UINT8 calcMove(UINT8 pos) {
  switch(pos) {
    case 1:
      return 248;
    case 2:
      return 243;
    case 3:
      return 238;
    case 4:
      return 233;
    case 5:
      return 228;
    case 6:
      return 223;
    default:
      (void)printf("Bad move position %u\r\n", pos);
      // enter error state
    
  }
  return 255;
}

// Updates LEDs and servos state for continue
void unpause(UINT8 servo) {
   if(!servos[servo].err) {
    if(servo == LED_SERVO){
      PORTB_BIT4 = 1;
    }
    servos[servo].pause = 0;
   }
}

// Updates LEDs and servo state to pause
void pause(UINT8 servo) {
   if(servo == LED_SERVO){
    PORTB_BIT4 = 0;
   }
   servos[servo].pause = 1;
}

// Updates LEDs and servo err state
void err(UINT8 code, UINT8 servo) {
  servos[servo].pause = 1;
  servos[servo].err = 1;
  if(servo == LED_SERVO){
  
    // bad opcode error
    if(code == 1){
      PORTB_BIT7 = 0;
    
    // nested loop error
    } else if(code == 2) {
      PORTB_BIT6 = 0;
    }
  }
}

// Remove err state
void clearErr(UINT8 servo) {
   if(servo == LED_SERVO){
    PORTB_BIT7 = 1;
    PORTB_BIT6 = 1;
   }
   servos[servo].err = 0;
}

// Set recipe end LED
void ending(UINT8 servo){
  if(servo == LED_SERVO) {
    PORTB_BIT5 = 0;
  }
}

//Start or restart recipe
void restart(UINT8 servo) {
  servos[servo].recipeIndex = 0;
  clearErr(servo);
  unpause(servo);
  if(servo == LED_SERVO) { 
    PORTB_BIT5 = 1;
  }
  
}

// Increment recipe Opcode by clock
void nextOp() {
  UINT8 i = 0;
  for(i; i < 2; i++) {
    if(!servos[i].pause) {
      if(servos[i].wait <= 0) {
        parseOpcode(servos[i].recipe[servos[i].recipeIndex], i);
        servos[i].recipeIndex++;
      }
    }
  }
}

// Calc wait time based on distance between positions
UINT8 waitTime(UINT8 newPos, UINT8 oldPos) {
  UINT8 diff;
  if(newPos > oldPos) {
    diff =  newPos - oldPos;
   } else if(oldPos > newPos) {
     diff = oldPos - newPos; 
   }
   if(diff <= 0) {
    diff = 1;
   }
  return 20*diff;
}

// Wait using semaphore like variable
void wait(UINT8 cycles, UINT8 servo) {
  servos[servo].wait = 0;
  servos[servo].wait +=cycles;
}

// Set register to new postion number
void move(UINT8 pos, UINT8 servo) {
  servos[servo].curPos = pos;
  *servos[servo].reg = calcMove(pos);
}

// Initialize variables in servo struct
Servo initServo(Servo s) {
  s.looping = 0;
  s.loopIndex = 0;
  s.wait = 0;
  s.pause = 1;
  s.recipeIndex = 0;
  s.curPos = 0;
  s.err = 0;
  return s;
}

// Setup PWM registers
void setupPWM(void) {
  PWME_PWME0 = 1;
  PWME_PWME1 = 1;
  
  PWMPOL_PPOL0 = 0;
  PWMPOL_PPOL1 = 0;
  
  PWMCLK_PCLK0 = 1;
  PWMCLK_PCLK1 = 1;
  
  PWMSCLA = 78;
   
  PWMPRCLK_PCKA0 = 0;
  PWMPRCLK_PCKA1 = 0;
  PWMPRCLK_PCKA2 = 0;
  
  PWMPER0 = 255; 
  PWMPER1 = 255;
  servos[0].reg = &PWMDTY0;
  servos[1].reg = &PWMDTY1;
  
}

// Setup led registers
void setupLed() {
  DDRB |= 0xF0;
  PORTB_BIT4 = 0;
  PORTB_BIT5 = 1;
  PORTB_BIT6 = 1;
  PORTB_BIT7 = 1;
};

// Parses and executes a command line statement
void parseCommand(UINT8 command, UINT8 servo) {
   UINT8 downcasedCharacter = downcase(command);
   switch(downcasedCharacter) {
    case 'p':
      // Pause
      pause(servo); 
      break;
    case 'c':
      //continue
      unpause(servo);
      break;
    case 'r':
      //move right
      if(servos[servo].curPos > 1) {
        move(servos[servo].curPos-1, servo);
        wait(20, servo);
      }
      break;
    case 'l':
      // Move left
      if(servos[servo].curPos < 6) {
        move(servos[servo].curPos+1, servo);
        wait(20, servo);
      }
      break;
    case 'n':
      //NOOP
      break;
    case 'b':
      //restart
      restart(servo);
      break;
    default: 
      (void)printf("Unknown character %c\r\n", downcasedCharacter); 
   }
}

// Parses and executes a recipe's opcode on a specific servo
void parseOpcode(UINT8 command, UINT8 servo){
   UINT8 opcode = (command & 0xE0)  >> 5;
   UINT8 param = command & 0x1F;
   UINT8 i = 0;
   switch(opcode) {
    case 1: // MOV
      if(param < 0 || param > 5) {
        err(1, servo); 
        break;
      }
      if(servos[servo].looping){
        servos[servo].loopCommands[servos[servo].loopIndex] = command;
        servos[servo].loopIndex++; 
      }
      move(param+1, servo);
      wait(waitTime(param, servos[servo].curPos), servo);
      break;
    
    case 2: //WAIT
      if(param < 0 || param > 31) {
        err(1, servo);
        break;
      }
      if(servos[servo].looping){
        servos[servo].loopCommands[servos[servo].loopIndex] = command;
        servos[servo].loopIndex++;
      }
      wait(param, servo);
      break;
    
    case 4: //LOOP START
      if(servos[servo].looping){
        err(2, servo);
        break; 
      }
      if(param < 0 || param > 31) {
        err(1, servo);
        break;
      }
      servos[servo].looping = 1;
      break;
    
    case 5:  //END LOOP
      if(!servos[servo].looping) {
        err(1, servo);
        break;
      }
      servos[servo].looping = 0;
      for(i; i < servos[servo].loopIndex; i++) {
        parseOpcode(servos[servo].loopCommands[i], servo);
        servos[servo].loopCommands[i] = 0;
      }
      
      servos[servo].loopIndex = 0;
      break;
    
    case 0: //END RECIPE
      servos[servo].recipeIndex = 0;
      pause(servo);
      clearErr(servo);
      ending(servo);
      break;
    default:
      err(1, servo);
      (void)printf("\r\nBad opcode %u", opcode);
      newLine(); 
      break;   
    
   }
}

// Sets prompt character on newline 
void newLine() {
  (void)printf("\r\n>"); 
}

// Downcase ascii character
UINT8 downcase(UINT8 character) {
  if(character < 0x61) {
     character ^= 0x20;
      
  }
  return character;
}

// Command line interface 
void cli(void) {
  UINT8 buffer[2] = {0};
  UINT8 tmp = 0;
  UINT8 index = 0;
  (void)printf(">");
  for(;;) {
    tmp = GetChar();
    if(tmp == 'x' || tmp == 'X') {
      newLine();
      continue;
    }
    if(tmp == '\r') {
      if(buffer[0] != 0 && buffer[1] != 0) {
       	parseCommand(buffer[0], 0);
        parseCommand(buffer[1], 1);
      }
      newLine();
      index = 0;
    } else {
      (void)printf("%c", tmp);
       buffer[index] = tmp;
       index++; 
    }
    if(index >= 2) {
      index = 0;
    }

  }
  
}
 
// Entry point of our application code
//--------------------------------------------------------------       
void main(void)
{
  InitializeSerialPort();
  InitializeTimer();
  setupPWM();
  setupLed();
  servos[0] = initServo(servos[0]);
  servos[0].recipe = &nestedLoop;
  servos[1] = initServo(servos[1]);
  servos[1].recipe = &testAllPos;
  cli();
}