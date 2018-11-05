/*
Schematic:

 VCC -+-`/\/\/'---+
      |   4.7k    |
      +-`/\/\/'-+ |
                | |
------+     +---+ |     +---------
  A   |     |     |     |
  R  5| ----|-----+---- | /CLK
  D   |     |           |       C
  U  6| ----+---------- | /DAT  6
  I   |                 |       4
  N  4| --------------- | /ATN
  O   |                 |
 -----+                 +---------

*/


//Data Lines from the c64
const int ATN = 4;
const int CLK = 5;
const int DAT = 6;

byte eoi = 0;   //EOI for recieving
byte myeoi = 0;   //EOI for sending
byte byteIn = 0x00;
byte haveGotData = 0; //Did get data yet? 0 = NO, 1 = YES
long savedTime = 0;
long timeTaken = 0;
long timeTemp = 0;
int atnBytes = 0; //Number of bytes recieved during ATN

void setup() {
  //start serial connection
  Serial.begin(115200);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }
  //Configure all input pullup resistors
  //These can be used as resistive outputs
  pinMode(ATN, INPUT_PULLUP); 
  digitalWrite(CLK, LOW);
  pinMode(CLK, INPUT);  //Allows it to be pulled low by other devices on the line
  digitalWrite(DAT, LOW);
  pinMode(DAT, INPUT);  //Allows it to be pulled low by other devices on the line

  Serial.println();
  Serial.println("Ready. v2.18");
}

void loop() {
  
  if (digitalRead(ATN) == LOW & digitalRead(CLK) == LOW & atnBytes < 2) {  //Computer is requesting to communicate
    //delayMicroseconds(10);    //Needed to prevent ATN errors
    Serial.println("ATN");
    int cat = getByte();
    atnBytes = atnBytes + 1;    //Increment number of bytes recieved during ATN
    //long timeTemp = micros();
    
  } else if (digitalRead(CLK) == HIGH & digitalRead(ATN) == HIGH & atnBytes >= 2) {  //(atnBytes >= 2) {
    if (haveGotData == 1 & micros()-timeTemp >=180) {   //Is it an EOI?
      pinMode(DAT, OUTPUT); //Set Data low to signal that an EOI has been recieved
      delayMicroseconds(70);
      pinMode(DAT, INPUT);  //Release Data line
    }

    Serial.println("Filename:");
    int pie = getByte();
    haveGotData = 1;
    timeTemp = micros();
  } else {
    //Serial.println(atnBytes);
  }
}

byte getByte() {
  //+---------------------=------+
  //|       MAIN BYTE LOOP       |
  //+----------------------------+
  eoi = 0;  //Reset EOI flag
  pinMode(DAT, OUTPUT);   //Pull the DATA line to TRUE to say "I'm Here!"

  while (digitalRead(CLK) == LOW) { //Wait for CLOCK line to go FALSE
  }

  pinMode(DAT, INPUT);   //Pull the DATA line to FALSE to say "READY"

  byteIn = shiftByteIn();     //Get the byte being sent from computer
  Serial.println(byteIn, HEX);
  delayMicroseconds(20);
  pinMode(DAT, OUTPUT);
  return byteIn;
}

byte shiftByteIn() {
  int temp = 0;
  byte myDataIn = 0;

  for (int i=0; i<=7; i++)
  {
    while (digitalRead(CLK) == HIGH) {   //Wait for the clock line to go to TRUE
    }
    while (digitalRead(CLK) == LOW) {  //Wait for clock line to go to FALSE
    }
    
    delayMicroseconds(4);   //Delay to make sure lines are stable
    temp = digitalRead(DAT);
    if (temp) {  
      //set the bit to 1
      myDataIn = myDataIn | (1 << i);
    } 
  }
  return myDataIn;
}

void turnAround() {
  
}

byte shiftByteOut(byte byteOut) {
  pinMode(CLK, OUTPUT);
  delayMicroseconds(60);
  pinMode(CLK, INPUT);    //Release Clock line to say "Ready to send Byte"

  while (digitalRead(DAT) == LOW) {   //Wait for listner to be ready
  }

  if (myeoi == 0) {
    delayMicroseconds(50);  //Not an EOI
  } else {
    delayMicroseconds(250); //Is an EOI
    while (digitalRead(DAT) == HIGH) {  //Wait for listner to acknowledge EOI
    }
    while (digitalRead(DAT) == LOW) {
    }
  }

  pinMode(CLK, OUTPUT);
  for (int i=0; i<=7; i++) {

  }
}

/*
 *  
  if (digitalRead(ATN) == LOW) {  //Computer is requesting to communicate
    delayMicroseconds(10);
    //+---------------------=------+
    //|       MAIN BYTE LOOP       |
    //+----------------------------+
 
    byte eoi = 0;
    do { //Loop until an EOI has been recieved
      pinMode(DAT, OUTPUT);   //Pull the DATA line to TRUE to say "I'm Here!"

      //delayMicroseconds(45);  //Make sure the computer sees the ready signal
      while (digitalRead(CLK) == LOW) { //Wait for CLOCK line to go FALSE
      }
  
      //When Ready, release the DATA line
      //Serial.println("Resopnding to request");
      pinMode(DAT, INPUT);   //Pull the DATA line to FALSE to say "READY"
  
      //Check for time it takes for the clock line to be pulled true
//      savedTime = micros();  //Get current time (Microseconds)
//      while (digitalRead(CLK) == HIGH) {   //Wait for Talker to respond
//      }
//      timeTaken = micros() - savedTime;  //See how long it took for a responce
//      if (timeTaken <= 200) {   //If the time taken was less than 190 uS, it was probablly not EOI
        
        byteIn = shiftByteIn();     //Get the byte being sent from computer
        Serial.println(byteIn, HEX);
//      } else {  //It took longer than 190 uS, so it must be an EOI

//        Serial.println("Got EOI");
//        pinMode(DAT, OUTPUT);   //Pull the DATA line to TRUE to say "I heard the EOI"
//        delayMicroseconds(60);  //Wait 60 Microseconds to ensure the processor caught the data
//        pinMode(DAT, INPUT); //Release DATA line
        
//        byteIn = shiftByteIn();     //Get the byte being sent from computer
//        Serial.println(byteIn, HEX);
//        eoi = 1;   //Stop looping
//      } 
    } while (eoi != 1);
    
    Serial.println("Done with ATN Loop");
    delayMicroseconds(20);  //Delay then release the data line
    pinMode(DAT, INPUT);
  } else {
    
  }
*/

