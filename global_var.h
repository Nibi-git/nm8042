#define PositionAddStep 1//4
#define PositionSub 7
#define PositionMax 1000



volatile unsigned int Sensitivity;


#define threshold5 255
#define threshold4 201
#define threshold3 81
#define threshold2 21
#define threshold1 6
/*
#define threshold5 255
#define threshold4 164
#define threshold3 61
#define threshold2 21
#define threshold1 6
*/
volatile unsigned char ToneNumber;
unsigned char LCDPrescaler;

unsigned long Integrator;
volatile unsigned int ReceivedSignal;

volatile unsigned int TimeTX;
volatile unsigned char TimeGuardAfterTXOFF, TimeGuardAfterRXON, TimeIntegration;

unsigned char IntegratorCycleCount;
volatile unsigned char IntegratorCycleEnd;


unsigned long EchoSumm;
unsigned char Echo;
unsigned int BaseValue;

#define ArrayLength 4
unsigned int ArrayReceivedSignal [ArrayLength];