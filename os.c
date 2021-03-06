/*******************************************
*	Name: Julian Torres
* CPTR 480
* 6/12/2018
*	File Name: os.c
*	Purpose: Holds simple OS functions including the scheduler. There are only two threads.
*******************************************/


//****************************************************************************
// os.c
// Runs on MSP432E401Y
// A very simple real time operating system with minimal features.
//     Modified 4/25/18  L.Aamodt
//
// Copyright 2015 by Jonathan W. Valvano, valvano@mail.utexas.edu
//    You may use, edit, run or distribute this file
//    as long as the above copyright notice remains
//****************************************************************************

#include "msp.h"

													// Sys. Handlers 12 to 15 Priority
#define NVIC_SYS_PRI3_R         (*((volatile uint32_t *)0xE000ED20)) 

// function definitions in osasm.s
void OS_DisableInterrupts(void); // Disable interrupts
void OS_EnableInterrupts(void);  // Enable interrupts
int32_t StartCritical(void);
void EndCritical(int32_t primask);
void StartOS(void);
void Scheduler(void); 
void PeriodicSensor(void);
void OS_Wait(int32_t *s);
void OS_Signal(int32_t *s);

#define NUMTHREADS  2        // maximum number of threads    
#define STACKSIZE   100      // number of 32-bit words in stack				

struct tcb{
  int32_t *sp;       // pointer to stack (valid for threads not running
  struct tcb *next;  // linked-list pointer
};

typedef struct tcb tcbType;
tcbType tcbs[NUMTHREADS];
tcbType *RunPt;
int32_t Stacks[NUMTHREADS][STACKSIZE];

uint32_t Counter = 0;

void OS_Signal(int32_t *s) {
	OS_DisableInterrupts();
	(*s) = (*s) + 1;
	OS_EnableInterrupts();
}

void OS_Wait(int32_t *s) {
	OS_DisableInterrupts();
	while((*s) == 0) {
		OS_EnableInterrupts();
		OS_DisableInterrupts();
	}
	(*s) = (*s) - 1; 
	OS_EnableInterrupts();
}

void Scheduler(void) {
	PeriodicSensor();			// runs every ms
	RunPt = RunPt->next;
}

// ******** OS_Init ************
// initialize operating system, disable interrupts until OS_Launch
// initialize OS controlled I/O & SysTick
// input:  none
// output: none
void OS_Init(void){
  OS_DisableInterrupts();
	SysTick->CTRL = 0;					// disable SysTick during setup
	SysTick->VAL = 0x000000;		// clear SysTick counter register. any value will do
	SysTick->CTRL = 0x07;				// enable SysTick  select system clock
															//    enable SysTick interrupt, auto reload
  NVIC_SYS_PRI3_R =(NVIC_SYS_PRI3_R&0x00FFFFFF)|0xE0000000; // priority 7
}

void SetInitialStack(int i){
  tcbs[i].sp = &Stacks[i][STACKSIZE-16]; // thread stack pointer
  Stacks[i][STACKSIZE-1] = 0x01000000;   // thumb bit
  Stacks[i][STACKSIZE-3] = 0x14141414;   // R14
  Stacks[i][STACKSIZE-4] = 0x12121212;   // R12
  Stacks[i][STACKSIZE-5] = 0x03030303;   // R3
  Stacks[i][STACKSIZE-6] = 0x02020202;   // R2
  Stacks[i][STACKSIZE-7] = 0x01010101;   // R1
  Stacks[i][STACKSIZE-8] = 0x00000000;   // R0
  Stacks[i][STACKSIZE-9] = 0x11111111;   // R11
  Stacks[i][STACKSIZE-10] = 0x10101010;  // R10
  Stacks[i][STACKSIZE-11] = 0x09090909;  // R9
  Stacks[i][STACKSIZE-12] = 0x08080808;  // R8
  Stacks[i][STACKSIZE-13] = 0x07070707;  // R7
  Stacks[i][STACKSIZE-14] = 0x06060606;  // R6
  Stacks[i][STACKSIZE-15] = 0x05050505;  // R5
  Stacks[i][STACKSIZE-16] = 0x04040404;  // R4
}

//******** OS_AddThread ***************
// add three foregound threads to the scheduler
// Inputs: three pointers to a void/void foreground tasks
// Outputs: 1 if successful, 0 if this thread can not be added
int OS_AddThreads(void(*task0)(void),
										void(*task1)(void)){

	int32_t status; 
  status = StartCritical();
  tcbs[0].next = &tcbs[1]; // 0 points to 1
  tcbs[1].next = &tcbs[0]; // 1 points to 2
										 
  SetInitialStack(0); Stacks[0][STACKSIZE-2] = (int32_t)(task0); // PC
  SetInitialStack(1); Stacks[1][STACKSIZE-2] = (int32_t)(task1); // PC
  RunPt = &tcbs[0];       // thread 0 will run first
  EndCritical(status);
  return 1;               // successful
}

///******** OS_Launch ***************
// start the scheduler, enable interrupts
// Inputs: number of system clock cycles for each time slice
//         (maximum of 24 bits)
// Outputs: none (does not return)
void OS_Launch(uint32_t SysCountValue){
	SysTick->LOAD = SysCountValue;		// counter reload value
	SysTick->CTRL = 0x07;							// enable SysTick  select system clock
																		//    enable SysTick interrupt, auto reload
  StartOS();                   // start on the first task
}

