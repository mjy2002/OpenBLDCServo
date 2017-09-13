#define Serial SERIAL_PORT_USBVIRTUAL
//#include <adafruit_feather.h>

//perhaps for faster ADC
//#define BUFSIZE   128
//uint16_t buffer[BUFSIZE] = { 0 };


//encoder pins
#define encoderPinA 0 //RXD0 PA11
#define encoderPinB 1 //TXD0 PA10


//hall sensor pins
#define hallA       A3 //PA04
#define hallB       A4 //PA05
#define hallC       A0 //PA02


//motor control pins
#define motorP1     5  //PA15
#define motorP2     6  //PA20
#define motorP3     9  //PA07
#define motorN1     10 //PA18
#define motorN2     11 //PA16
#define motorN3     12 //PA19


//control input pins
#define stepIN      22 //MISO PA12
#define dirIN       23 //MOSI PB10


//current input pins
#define current0    A5 //PB02
#define current1    A1 //PB08
#define current2    A2 //PB09


//program variables
#define MAXCURRENT 0.7 //peak rotational current. Actual max current hit may be slightly higher
#define ENC_RESOLUTION 4000 //how many ticks per revolution (actual value, so for quadrature, multiply ticks/rev by 4)
#define ROLLING_SIZE 5 //size of rolling average for speed calculations

volatile long encoderTicks = 0;
volatile long stepperTicks = 0;

int lastHall = 0; //last hall value
bool lastRev = 0; //last reverse bool
bool stateChangeFlag = 0; //keeps track if coils are changing
int state = 0; //current coil state, from 0 to 5
double correctedOutput = 0;
uint16_t currentThresh = MAXCURRENT * 124.1212; //converts amps to analog steps for 12bit ADC
bool coilGndFlag = 0;

//PID defs
unsigned long lastTime;
double Input, Output, Setpoint;
double ITerm, lastInput;
double kp, ki, kd;
int SampleTime = 10; //10us
double outMin, outMax;

//troubleshooting
long hallChanges = 0;

void setup() 
{
  //encoder IO setup
  pinMode(encoderPinA, INPUT_PULLUP);
  pinMode(encoderPinB, INPUT_PULLUP);
  attachInterrupt(encoderPinA, handleA, CHANGE);
  attachInterrupt(encoderPinB, handleB, CHANGE);

  //hall sensor IO setup
  pinMode(hallA, INPUT_PULLUP);
  pinMode(hallB, INPUT_PULLUP);
  pinMode(hallC, INPUT_PULLUP);

  //motor control IO setup
  pinMode(motorP1, OUTPUT);
  pinMode(motorP2, OUTPUT);
  pinMode(motorP3, OUTPUT);
  pinMode(motorN1, OUTPUT);
  pinMode(motorN2, OUTPUT);
  pinMode(motorN3, OUTPUT);
  disableMotor();

  //control input IO setup
  pinMode(stepIN, INPUT);
  pinMode(dirIN, INPUT);
  attachInterrupt(stepIN, newStep, RISING); 

  //PID setup
  SetOutputLimits(-150, 150); //max speeds in RPM
  PIDinit();
  SetTunings(1, 1, 1);

  //faster ADC setup
//  ADC.begin(analogPin);
//  ADC.setBuffer(buffer, BUFSIZE);
//  ADC.setSampleRate(ADC_SMPR_1_5);
//  ADC.start();

  Serial.begin(115200);

  //TROUBLESHOOTING
  Setpoint = 1000;
//  Output = 20;
}

//basic loop prototype
void loop()
{
  double rollSpeed[ROLLING_SIZE];
  double avgSpeed = 0; //average rotational speed
  uint32_t lastMicro = 0; //last time, for speed calc
  long lastTick = 0;      //last enc pos, for speed calc
  uint32_t rollCount = 0;

  //initialize rolling speed array
  for(uint8_t i = 0; i < ROLLING_SIZE; i++)
    rollSpeed[i] = 0;
  
  while(true)
  {
    //setpoint and currentpoint are both known. First PID calculate speed
    Input = encoderTicks;
    PIDcompute();
    updateSpeed(rollSpeed[rollCount], lastMicro, lastTick); //updates current rotational speed
    rollCount++; //increment rolling count
    rollCount %= ROLLING_SIZE;

    //calculate average speed
    avgSpeed = 0;
    for(uint8_t i = 0; i < ROLLING_SIZE; i++)
      avgSpeed += rollSpeed[i];
    avgSpeed /= ROLLING_SIZE;

//    Serial.println(avgSpeed);
//    Serial.println(Output);
//    Serial.println(encoderTicks);
//    Serial.println();

    //move motor
    moveMotor(avgSpeed);
    Setpoint++;
  }
}

//run loop
//void loop()
//{
//  //troubleshooting and calibration stuffs
//  unsigned long counter = 0;
//  unsigned long minCt = 0;
//  unsigned long maxCt = 0;
//  double sumPt = 0;
//  double sumMax = 0;
//  double sumMin = 0;
//  double sumOvr = 0;
//  double sumUdr = 0;
//  double lastEnc = 0;
//  bool risefallFlag = 0;
//
////  Setpoint *= -1;
//
//  while(counter < 1000)
//  {
//    Setpoint += 2;
//    Input = encoderTicks;
//    PIDcompute();
//    moveMotor();
//  
//    //loop calculations
////    sumPt += encoderTicks;
////    counter++;
////    if (encoderTicks > Setpoint)
////      sumOvr += (encoderTicks - Setpoint);
////    else if (encoderTicks < Setpoint)
////      sumUdr += (encoderTicks - Setpoint);
////    if ((risefallFlag) && (encoderTicks < lastEnc))
////    {
////      risefallFlag = 0;
////      sumMax += encoderTicks;
////      maxCt++;
////    }
////    else if ((!risefallFlag) && (encoderTicks > lastEnc))
////    {
////      risefallFlag = 1;
////      sumMin += encoderTicks;
////      minCt++;
////    }
////    lastEnc = encoderTicks;
//  }
//  
////  Serial.print("\n\nAvg value: ");
////  Serial.println(sumPt / counter);
////  Serial.print("Avg over: ");
////  Serial.println(sumOvr / counter);
////  Serial.print("Avg under: ");
////  Serial.println(sumUdr / counter);
////  Serial.print("Avg peak: ");
////  Serial.println(sumMax / maxCt);
////  Serial.print("Avg trough: ");
////  Serial.println(sumMin / minCt);
//}

void updateSpeed(double &curSpeed, uint32_t &lastMicro, long &lastTick)
{
  long tickDif = encoderTicks - lastTick;
  uint32_t microDif = micros() - lastMicro;
  if (microDif < 0)
    Serial.println("ERROR, LAST MICRO LESS THAN CURRENT MICRO");

  curSpeed = tickDif;
  curSpeed /= ENC_RESOLUTION;
  curSpeed /= microDif;
  curSpeed *= 60000000;

  //update history values
  lastMicro += microDif;
  lastTick += tickDif;
}

void moveMotor(const double curSpeed)
{
  bool reversed = 0;
  bool overSpeed = 0;
  double setCurrent = 0;
  correctedOutput = Output;

  if (correctedOutput < 0) //check if motor should spin in reverse
  {
    correctedOutput *= -1;
    reversed = 1;
  }

  int hallState = readHallState(); //get hall position
  if ((lastRev != reversed) || (lastHall != hallState)) //determine if coils need to change
    stateChangeFlag = 1;
    
  lastRev = reversed;
  lastHall = hallState;

  //check if current speed is greater than desired. Otherwise, scales current based on deviation from set speed
  if (((!reversed) && (curSpeed > Output)) || ((reversed) && (curSpeed < Output)))
    overSpeed = 1;
  else
    setCurrent = (1 + fabs(Output - curSpeed)) / 20;

  if (setCurrent >= 1)
    setCurrent = currentThresh;
  else
    setCurrent *= currentThresh;

  if (stateChangeFlag) //if change in hall reading or direction, set new coils
  {
    hallChanges++; //troubleshooting finds speed
    disableMotor(); //disable motor to reset coils
    stateChangeFlag = 0;

    //set new state
    switch (hallState)
    {
      case 4: { //100
        state = 0;
        break;
      }
      case 5: { //101
        state = 1;
        break;
      }
      case 1: { //001
        state = 2;
        break;
      }
      case 3: { //011
        state = 3;
        break;
      }
      case 2: { //010
        state = 4;
        break;
      }
      case 6: { //110
        state = 5;
        break;
      }
    }

    if (reversed) //invert state if reversed direction
    {
      state -= 2;
      if (state < 0)
        state += 6;
    }
    if (!overSpeed)
      setCoil(); //turn on appropriate fets for new coil as long as not overspeed
  }
  else //otherwise if same coil being used, check for overcurrent. If over, disable. If under, keep enabled
  {
    uint16_t coilCurrent = readCurrent(); //read current is very slow because analog read sucks
    if (((coilCurrent > setCurrent) || (overSpeed)) && (!coilGndFlag)) //if current over thresh and previously below, or overspeed, ground all coils
    {
      stateGND(); //disable all P channel, enable all N channel
      coilGndFlag = 1;
    }
    else if ((coilCurrent < setCurrent) && (!overSpeed) && (coilGndFlag)) //if curret below thresh and was previously above, turn back on and reset flag
    {
      disableMotor();
      setCoil(); 
      coilGndFlag = 0;
    }
  }
}

void disableMotor() //turns off all mosfets using registers
{
//  PORT->Group[PORTA].OUTCLR.reg = 111011000000010000000; //not currently working but should be faster
  PORT->Group[PORTA].OUTCLR.reg = (1<<15);
  PORT->Group[PORTA].OUTCLR.reg = (1<<20);
  PORT->Group[PORTA].OUTCLR.reg = (1<<7);
  PORT->Group[PORTA].OUTCLR.reg = (1<<18);
  PORT->Group[PORTA].OUTCLR.reg = (1<<16);
  PORT->Group[PORTA].OUTCLR.reg = (1<<19);

  delayMicroseconds(1); //delay time to prevent shoot-through. 100ns should be sufficient, 1us is being used
}

int readHallState()
{
  uint32_t readval = PORT->Group[PORTA].IN.reg; //reads portA
  int hallState = 0;
  if (readval & (1<<4))
    hallState += 4;
  if (readval & (1<<5))
    hallState += 2;
  if (readval & (1<<2))
    hallState += 1;
  return hallState;
}

void setCoil() //set pwm for new active coil
{
  switch (state)
  {
    case 0: {state0();
    break;}
    case 1: {state1();
    break;}
    case 2: {state2();
    break;}
    case 3: {state3();
    break;}
    case 4: {state4();
    break;}
    case 5: {state5();
    break;}
  }
}

uint16_t readCurrent() //read current for active mosfet
{
  uint16_t coilCurrent = 0;
  if ((state == 0) || (state == 1))
    coilCurrent = analogRead(current0);
  if ((state == 2) || (state == 3))
    coilCurrent = analogRead(current1);
  if ((state == 4) || (state == 5))
    coilCurrent = analogRead(current2);
  return coilCurrent;
}

/* PID ALGORITHMS */
void PIDcompute()
{
    /*Compute all the working error variables*/
    double error = Setpoint - Input;
    ITerm+= (ki * error);
    if(ITerm > outMax) ITerm= outMax;
    else if(ITerm < outMin) ITerm= outMin;
    double dInput = (Input - lastInput);

    /*Compute PID Output*/
    Output = kp * error + ITerm - kd * dInput;
    if(Output > outMax) Output = outMax;
    else if(Output < outMin) Output = outMin;

    /*Remember some variables for next time*/
    lastInput = Input;
}

void SetTunings(double Kp, double Ki, double Kd)
{
   if (Kp<0 || Ki<0|| Kd<0) return;
 
  double SampleTimeInSec = ((double)SampleTime)/1000;
   kp = Kp;
   ki = Ki * SampleTimeInSec;
   kd = Kd / SampleTimeInSec;
}
 
void SetSampleTime(int NewSampleTime)
{
   if (NewSampleTime > 0)
   {
      double ratio  = (double)NewSampleTime
                      / (double)SampleTime;
      ki *= ratio;
      kd /= ratio;
      SampleTime = (unsigned long)NewSampleTime;
   }
}
 
void SetOutputLimits(double Min, double Max)
{
   if(Min > Max) return;
   outMin = Min;
   outMax = Max;
 
   if(Output > outMax) Output = outMax;
   else if(Output < outMin) Output = outMin;
 
   if(ITerm > outMax) ITerm= outMax;
   else if(ITerm < outMin) ITerm= outMin;
}
 
void PIDinit()
{
   lastInput = Input;
   ITerm = Output;
   if(ITerm > outMax) ITerm= outMax;
   else if(ITerm < outMin) ITerm= outMin;
}

/* EXTERNAL INTERRUPT PROTOCOLS */
void handleA()
{
  bool aState = 0, bState = 0; //sets states low as default
  uint32_t readval = PORT->Group[PORTA].IN.reg; //reads portA
  
  if (readval & (1<<11)) //if register for pin A is high, make aState high
    aState = 1;
  if (readval & (1<<10)) //same for B
    bState = 1;
    
  if ((!bState && aState) || (bState && !aState)) //increment or decrement encoder count appropriately
    encoderTicks++;
  else
    encoderTicks--;
}

void handleB()
{
  bool aState = 0, bState = 0;
  uint32_t readval = PORT->Group[PORTA].IN.reg;
  
  if (readval & (1<<11))
    aState = 1;
  if (readval & (1<<10))
    bState = 1;
    
  if ((!bState && !aState) || (bState && aState))
    encoderTicks++;
  else
    encoderTicks--;
}

void newStep()
{
  bool dir = digitalRead(dirIN);
  if (dir)
    stepperTicks++;
  else
    stepperTicks--;
}

/* MOSFET STATES */
void state0() //P2N3
{
  PORT->Group[PORTA].OUTSET.reg = (1<<20);
  PORT->Group[PORTA].OUTSET.reg = (1<<19);
}

void state1() //P1N3
{
  PORT->Group[PORTA].OUTSET.reg = (1<<15);
  PORT->Group[PORTA].OUTSET.reg = (1<<19);
}

void state2() //P1N2
{
  PORT->Group[PORTA].OUTSET.reg = (1<<15);
  PORT->Group[PORTA].OUTSET.reg = (1<<16);
}

void state3() //P3N2
{
  PORT->Group[PORTA].OUTSET.reg = (1<<7);
  PORT->Group[PORTA].OUTSET.reg = (1<<16); 
}

void state4() //P3N1
{
  PORT->Group[PORTA].OUTSET.reg = (1<<7);
  PORT->Group[PORTA].OUTSET.reg = (1<<18);
}

void state5() //P2N1
{
  PORT->Group[PORTA].OUTSET.reg = (1<<20);
  PORT->Group[PORTA].OUTSET.reg = (1<<18);
}

void stateGND() //N1N2N3
{
  PORT->Group[PORTA].OUTCLR.reg = (1<<15);
  PORT->Group[PORTA].OUTCLR.reg = (1<<20);
  PORT->Group[PORTA].OUTCLR.reg = (1<<7);
  delayMicroseconds(1); //small delay here, is it needed?
  PORT->Group[PORTA].OUTSET.reg = (1<<16);
  PORT->Group[PORTA].OUTSET.reg = (1<<18);
  PORT->Group[PORTA].OUTSET.reg = (1<<19);
}

