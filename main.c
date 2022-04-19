//программа импульсного металлоискателя 8042 V1 - на основе прошивки 1.0b demo, скачанной с сайта
//--no_cross_call  - ОБЯЗАТЕЛЬНО!!!
//avrdude -p t2313 -P avrdoper -c stk500v2 -U flash:w:$PROJ_DIR$\Release\Exe\$PROJ_FNAME$.hex:i -U lfuse:w:0xFF:m -U hfuse:w:0xDB:m -U efuse:w:0xFF:m -U lock:w:0x3C:m
#include <ioavr.h>
#include <intrinsics.h>
#include <stdio.h>
#include <stdlib.h>
#include "ina90.h"

#include "lcd.h"
#include "timeout.h"

#include "hardware.h"
#include "global_var.h"

#define START_DELAY 500 //милисекунд для установления режимов усилителя по постоянному току

//#define ENABLE_LCD // если включено - светодиоды не используются
#define ENABLE_SOUND
//#define ENABLE_USART
#define OLD_AVERAGE_FILTER  // усредняющий и помехоподавляющий фильтр
//#define FAST_AVERAGE_FILTER  // экспериментальный фильтр в разработке
#define USE_BATTERY_METER  // использовать пин для измерения степени разряженности аккума
//#define USE_MANUAL_TIME_TX // задать время накачки вручную - для особых датчиков
#define ManualTimeTX 0x75

#define BoardV3 // на плате 3 версии находятся ускоряющие конденсаторы и открывающий вспомогательный транзистор в драйвере полевика

#ifdef BoardV3
  #define CAP_BOOST_TIME 0//5
  #define TIME_GUARD 0x0c
  #define START_TIME_TX 59 //65
#else
  #define CAP_BOOST_TIME 0
  #define TIME_GUARD 0x0a
  #define START_TIME_TX 0x41
#endif

//#define UTC_DELAY_1 6
//#define UTC_DELAY_2 6

#define baudrate 38400 //156248 //38400

void LowBatIndicationLoop (void);

void __watchdog_init (void)
{
//запускаю сторожевой таймер на 2 секунды
__watchdog_reset ();
WDTCR |= ((1<<WDCE)|(1<<WDE));
WDTCR = (1<<WDE)|(7<<WDP0);
__watchdog_reset ();
}

void InitTimers (void)
{
TCCR1A = 0x00;
TCCR1B |= ((1<<CS10) | (1<<WGM12));
OCR1A = 58500; //50000+6; //0xc350; //0xc350;

TCCR0B |= (4<<CS00);

TIMSK |= ((1<<TOIE0) | (1<<OCIE1A));

ACSR |= (1<<ACIC);		
}

void InitPorts (void)
{
/*
PortPot |= (1<<Pot);
PortPotDir |= (1<<Pot);

PortKeys |= ((1<<Q2) | (1<<Q0) | (1<<Q1));
PortKeysDir |= ((1<<Q2) | (1<<Q0) | (1<<Q1));

PortSpeakerDir |= (1<<Speaker);
*/
#ifndef ENABLE_LCD
PortLeds |= ((1<<Led1) | (1<<Led2) | (1<<Led3) | (1<<Led4) | (1<<Led5) | (1<<Led6));
PortLedsDir |= ((1<<Led1) | (1<<Led2) | (1<<Led3) | (1<<Led4) | (1<<Led5) | (1<<Led6));
#endif //ENABLE_LCD

/*
#ifdef USE_BATTERY_METER  //по идее это просто вход, поэтому можно просто включить подтяжку, на случай неиспользования измерителя
PortBatMeter |= (1<<BatMeter); //
#endif //USE_BATTERY_METER
*/

PortSys |=    ((1<<Pot) | (1<<Q2) | (1<<Q0) | (1<<Q1) | (1<<BatMeter));
PortSysDir |= ((1<<Pot) | (1<<Q2) | (1<<Q0) | (1<<Q1) | (1<<Speaker));
}

void InitUsart (unsigned int baud)
{
UBRRH = (unsigned char)(baud>>8);
UBRRL = (unsigned char)baud;

UCSRB |= (1<<TXEN);
}

void USARTSendChar (unsigned char data)
{
#ifdef ENABLE_USART
while (!( UCSRA & (1<<UDRE))); // ждем, пока отправится предыдущий байт
UDR = data;
#endif //ENABLE_USART
}

void InitMode (void)
{
IntegratorCycleEnd = 0x00; // переменная глобальная, поэтому равна 0 после RESET

//устанавливаем начальные временные интервалы

TimeTX = START_TIME_TX-CAP_BOOST_TIME;
TimeGuardAfterTXOFF = 0; // переменная глобальная, поэтому равна 0 после RESET
TimeGuardAfterRXON = 0;// + CAP_BOOST_TIME;// + UTC_DELAY_1; // переменная глобальная, поэтому равна 0 после RESET

TimeIntegration = 0x10;
__enable_interrupt();
}

inline void GetPotPosition (void)
{
unsigned int counter = 0;
__disable_interrupt();
PortPotDir &= ~(1<<Pot); //переключаем выход на вход
PortPot &= ~(1<<Pot);

while (PinPot & (1<<Pot)) { counter += PositionAddStep; } //ждем, пока напряжение упадет до порогового

//UDR = (unsigned char)counter;

PortPot |= (1<<Pot); //переключаем вход на выход
PortPotDir |= (1<<Pot);
counter -= PositionSub;

if ((counter > PositionMax) || (counter == 0)) counter = 1; // проверяем, не вылезло ли за пределы
Sensitivity = counter;

//UDR = (unsigned char)Sensitivity;
}

__flash signed int segmentsDec[5]={10000,1000,100,10,1};
unsigned char String[5];

void CharToStringDec(signed int inp)
{
unsigned char i;
String[0]=String[1]=String[2]=String[3]=String[4]=0;
// перевод
for(i=0;i<5;)
  {
  if((inp-segmentsDec[i])>=0)
    {
    inp-=segmentsDec[i];
    String[i]++;
    }
  else i++;
  }
}

void LedBarUpdate (unsigned char level)
{
if      (level >= threshold5) {  PortLeds = ((1<<Led1) | (1<<Led2) | (1<<Led3) | (1<<Led4) | (1<<Led5));   ToneNumber = 5; }
else if (level >= threshold4) {  PortLeds = ((1<<Led1) | (1<<Led2) | (1<<Led3) | (1<<Led4) | (1<<Led6));   ToneNumber = 4; }
else if (level >= threshold3) {  PortLeds = ((1<<Led1) | (1<<Led2) | (1<<Led3) | (1<<Led5) | (1<<Led6));   ToneNumber = 3; }
else if (level >= threshold2) {  PortLeds = ((1<<Led1) | (1<<Led2) | (1<<Led4) | (1<<Led5) | (1<<Led6));   ToneNumber = 2; }
else if (level >= threshold1) {  PortLeds = ((1<<Led1) | (1<<Led3) | (1<<Led4) | (1<<Led5) | (1<<Led6));   ToneNumber = 1; }
else                          {  PortLeds = ((1<<Led2) | (1<<Led3) | (1<<Led4) | (1<<Led5) | (1<<Led6));   ToneNumber = 0; }
}

#ifdef ENABLE_LCD
static __flash char str01 [] = " -<>+       "; //+000
static __flash char str02 [] = "Low Bat"; //+000
static __flash char str03 [] = "Wait"; //+000


void LCDBarUpdate (signed int level)
{
unsigned char col, pos;//, t;
signed int t;

//if (level < 0) t = 0;
if (level < 256) t = (signed int)level;
else t = 255;

if      (level >= threshold5) { ToneNumber = 5; }
else if (level >= threshold4) { ToneNumber = 4; }
else if (level >= threshold3) { ToneNumber = 3; }
else if (level >= threshold2) { ToneNumber = 2; }
else if (level >= threshold1) { ToneNumber = 1; }
else                          { ToneNumber = 0; }

if (++LCDPrescaler > 2) 
    {
    LCDPrescaler = 0;

    lcd_gotoxy(0,0);//здесь показать уровень
    lcd_puts_p(str01);
    
    /*
    if (level < 0)  {  lcd_putc('-');  }
      else    {    lcd_putc(' ');    }
    */
    CharToStringDec(level); // конвертация
    
    for (unsigned char v=1; v<5; v++) lcd_putc(String[v]+0x30);
    
    lcd_gotoxy(0,1);
    col = (t+48) / 16;//рассчитываем номер знакоместа для рисования палки
    
    if (col>0) for (unsigned char z=0; z<col; z++) lcd_putc(0x05);//рисуем несколько полностью закрашенных знакомест
    
    //рассчитываем номер символа для отображения в знакоместе //5 вариантов
    
    pos = (t+48) - (col*16); // остаток
    pos = (pos)/3;
    
    lcd_putc(pos); //0x00+
    
    for (unsigned char t=0; t<16; t++) lcd_putc(' '); //не чистим экран, а заполняем пробелами
    }
}

#endif //ENABLE_LCD

void DelayUnits (unsigned long time)
{
while (time--) {}
}

#pragma vector = TIMER1_COMPA_vect //вызывается раз в 5мс
__interrupt void KeyDrive (void)
{
//unsigned char temp = 10;
unsigned int ICRData = ICR1; //получаем данные о времени срабатывания компаратора
PortKeys &= ~(1<<Q1); //выключаем первый усилитель
DelayUnits (10);
PortKeys &= ~(1<<Q0); //включаем накачку катушки
DelayUnits (TimeTX);
PortKeys |= (1<<Q0); //выключаем накачку катушки
DelayUnits (TimeGuardAfterTXOFF);
PortKeys |= (1<<Q1); //включаем первый усилитель
DelayUnits (TimeGuardAfterRXON);
PortKeys &= ~(1<<Q2); //включаем интегратор
DelayUnits (TimeIntegration);
PortKeys |= (1<<Q2); //выключаем интегратор

IntegratorCycleCount++;
Integrator += ICRData;

//__enable_interrupt(); //вероятно таки придется разрешить здесь прерывания

if (IntegratorCycleCount > 7)
  {
  IntegratorCycleCount = 0;
  Integrator = (Integrator >> 3); // деление на 8
  ReceivedSignal = (unsigned int)Integrator;
  //UDR = ReceivedSignal>>8;
  IntegratorCycleEnd = 0xFF;
  Integrator = 0;
  }

GetPotPosition ();
__watchdog_reset ();
}

unsigned char SoundCycleCount;

#pragma vector = TIMER0_OVF0_vect 
__interrupt void Sound (void)
{
TCNT0 = 0xFE; // перезагрузить таймер
if ((ToneNumber != 0) && (SoundCycleCount == 0))
  {
  SoundCycleCount = 16 - ToneNumber;
#ifdef ENABLE_SOUND      
  if (PortSpeaker & (1<<Speaker)) PortSpeaker &= ~(1<<Speaker); // speaker toggle
    else PortSpeaker |= (1<<Speaker);
#endif //ENABLE_SOUND       
  }
if (SoundCycleCount) SoundCycleCount--;

#ifdef ENABLE_SOUND 
if (ToneNumber == 0) PortSpeaker &= ~(1<<Speaker); // не потребляем динамиком лишний ток
#endif //ENABLE_SOUND 
}

void LowBatIndicationLoop (void) // отсюда никогда не выходим
{
#ifndef ENABLE_LCD      //светодиодный столбец
PortLeds = ((1<<Led1) | (1<<Led2) | (1<<Led3) | (1<<Led4) | (1<<Led5) | (1<<Led6)); //тушим все диоды
#endif //ENABLE_LCD

//TimeTX = 0; //Полная накачка уже ни к чему
PortSysDir &= ~(1<<Q0); //Выключаем ключ совсем - Полная накачка уже ни к чему
#ifdef ENABLE_LCD //экран
lcd_clrscr();
lcd_gotoxy(4,0);
lcd_puts_p(str02);
#endif //ENABLE_LCD 

while (1)// батарея разряжена
    {
    __delay_cycles((CtrlClockRate/1000)*500);
    if (ToneNumber == 1) ToneNumber =0;
      else ToneNumber = 1;
      
    #ifndef ENABLE_LCD      //светодиодный столбец
    if (PortLeds & (1<<Led1)) PortLeds &= ~(1<<Led1); // мигаем зеленым диодом с интервалом 0,5с
      else PortLeds |= (1<<Led1);
    #endif //ENABLE_LCD
    }
}


int main (void)
{
signed int Ka=0, Kb=0, Kc=0, Kd=0, temp;//, tempdebug;

InitPorts ();

#ifdef ENABLE_LCD
lcd_init(LCD_DISP_ON);

//lcd_clrscr();
lcd_gotoxy(6,0);
lcd_puts_p(str03);
#endif //ENABLE_LCD

#ifdef ENABLE_USART
InitUsart (((CtrlClockRate/16)/baudrate)-1);
#endif //ENABLE_USART
InitTimers ();
InitMode ();
__delay_cycles((CtrlClockRate/1000)*START_DELAY); // стартовая задержка
__watchdog_init ();

//    unsigned int tem;

for (unsigned char i=0; i<8; i++)
  {
  while (IntegratorCycleEnd == 0) {} // ждем пока пройдет интегрирование
  EchoSumm = EchoSumm + ReceivedSignal;
  

//   tem = ReceivedSignal >> 8;  
//  USARTSendChar ((unsigned char)tem);
//  USARTSendChar ((unsigned char)ReceivedSignal); 
  
  IntegratorCycleEnd = 0;
  }

//USARTSendChar ((unsigned char)EchoSumm);


EchoSumm = EchoSumm >> 8; // деление на 256
Echo = (unsigned char)(EchoSumm);

//USARTSendChar (Echo);

Echo -= 0x32;

//USARTSendChar (Echo);   // эхо, полученное при калибровке

if (Echo >= 0x19) // катушка в порядке, эхо достаточной величины
  {
  if      (Echo <= 0x37) {Ka = 0x19; Kb = 0x37; Kc = 0xA0; Kd = 0x8C; }
  else if (Echo <= 0x4B) {Ka = 0x37; Kb = 0x4B; Kc = 0x8C; Kd = 0x67; }
  else if (Echo <= 0x52) {Ka = 0x4B; Kb = 0x52; Kc = 0x67; Kd = 0x52; }
  else if (Echo <= 0x5F) {Ka = 0x52; Kb = 0x5F; Kc = 0x52; Kd = 0x41; }
  
  temp = Echo - Ka;
  temp = temp * (Kd - Kc);
  temp = Kc + (temp / (Kb - Ka));

#ifndef USE_MANUAL_TIME_TX
  TimeTX = temp - CAP_BOOST_TIME;  //устанавливаем рабочие тайминги
  if (Echo >= 0x60) TimeTX = 0x41 - CAP_BOOST_TIME;
#endif
  
#ifdef USE_MANUAL_TIME_TX 
  TimeTX = ManualTimeTX;
#endif //USE_MANUAL_TIME_TX 

  TimeGuardAfterTXOFF = TIME_GUARD; 
  TimeGuardAfterRXON = 0x00;// + CAP_BOOST_TIME;// + UTC_DELAY_1 + UTC_DELAY_2;
  TimeIntegration = 0x32;
  
  USARTSendChar (TimeTX);  // вычисленное время накачки
/*
  for (unsigned char i=0; i<10; i++) // пропускаем 10 циклов для устаканивания
    {
   while (IntegratorCycleEnd == 0) {} // пропускаем 1 цикл
   IntegratorCycleEnd = 0;
    }  
*/

while (IntegratorCycleEnd == 0) {} // пропускаем 1 цикл, этого достаточно
IntegratorCycleEnd = 0;  
  
  
//unsigned int tem;  
  

  EchoSumm = 0;
  for (unsigned char i=0; i<32; i++)
    {
    while (IntegratorCycleEnd == 0) {} // ждем пока пройдет интегрирование
/*    
      tem = ReceivedSignal >> 8;
  
  USARTSendChar ((unsigned char)tem);  
  USARTSendChar ((unsigned char)ReceivedSignal);
*/  
    
    EchoSumm += ReceivedSignal;
    IntegratorCycleEnd = 0;
    }
  EchoSumm = EchoSumm >> 5; // деление на 32
  BaseValue = (unsigned int)EchoSumm; // получили базовое значение при отсутствии металла
  
/*
  tem = BaseValue >> 8;
  
  USARTSendChar ((unsigned char)tem);  
  USARTSendChar ((unsigned char)BaseValue);
*/
  
  //USARTSendChar (BaseValue>>8); //базовое значение
  
  while (1) // основной цикл обнаружения металла
    {
    while (IntegratorCycleEnd == 0) {} // ждем пока пройдет интегрирование
    IntegratorCycleEnd = 0;
    
#ifdef OLD_AVERAGE_FILTER   //усреднитель сигнала
    EchoSumm = 0;
    for (unsigned char z=0; z<(ArrayLength-1); z++)  { ArrayReceivedSignal [z] = ArrayReceivedSignal [z+1];  } // сдвинули массив к 0 ячейке
    ArrayReceivedSignal [ArrayLength-1] = ReceivedSignal; // в последнюю ячейку вносим свежее значение
    
    for (unsigned char z=0; z<ArrayLength; z++) EchoSumm += ArrayReceivedSignal [z]; // суммируем последние показания
    ReceivedSignal = (unsigned int)(EchoSumm / ArrayLength);
#endif //OLD_AVERAGE_FILTER

#ifdef FAST_AVERAGE_FILTER   //усреднитель сигнала (быстрый)

#endif //OLD_AVERAGE_FILTER    
    
#ifdef USE_BATTERY_METER  //измерение напряжения аккума
    PortBatMeterDir |= (1<<BatMeter); // назначаем как выход, для преодоления гистерезиса
    __delay_cycles((CtrlClockRate/1000000)*2); // чтобы устаканилось
    PortBatMeterDir &= ~(1<<BatMeter); // назначаем как вход
    __delay_cycles((CtrlClockRate/1000000)*2); // чтобы устаканилось
    
    if (!(PinBatMeter & (1<<BatMeter))) LowBatIndicationLoop (); // и меряем на порту 2,10 В
#endif //USE_BATTERY_METER
    
    //UDR = ReceivedSignal;
    
      //tem = ReceivedSignal >> 8;
  
  //USARTSendChar ((unsigned char)ReceivedSignal);
  //USARTSendChar ((unsigned char)tem);
    
    temp = ReceivedSignal - BaseValue; // сравнение с опорным значением
    
    /*
    tempdebug = temp + 127;
    if (tempdebug > 255) tempdebug = 255;
    if (tempdebug < 0) tempdebug = 0;
    UDR = tempdebug;
    */
    
    temp += temp; // разницу удваиваем со знаком
    //temp += temp; // разницу удваиваем со знаком
    //temp = 250;
    temp = temp / ((signed int)Sensitivity);
    //USARTSendChar ((unsigned char)Sensitivity);
    
    #ifndef ENABLE_LCD      //светодиодный столбец
    if (temp < 0) temp = 0; // следим за рамками
    if (temp > 254) temp = 255;
    USARTSendChar ((unsigned char)temp); // результат
    LedBarUpdate ((unsigned char)temp);
    #endif //ENABLE_LCD
    
    #ifdef ENABLE_LCD // тут своя процедура вывода полосы
    if (temp < -48) temp = -48; // следим за рамками
    //if (temp > 9998) temp = 9999;
    
    
    //if (++LCDPrescaler > 2) 
    //{
    //LCDPrescaler = 0;
    LCDBarUpdate (temp);
    //}
    #endif //ENABLE_LCD
    }
  }
  else LowBatIndicationLoop ();
}




