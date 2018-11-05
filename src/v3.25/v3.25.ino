/*
Schematic:

 VCC -+-`/\/\/'---+
      |   4.7k    |
      +-`/\/\/'-+ |
                | |
------+     +---+ |     +---------
  A   |     |     |     |
  R  6| ----|-----+---- | /CLK
  D   |     |           |       C
  U  7| ----+---------- | /DAT  6
  I   |                 |       4
  N  5| --------------- | /ATN
  O   |                 |
      |                 +---------
      |
      |                 +---------
     9| --------------- | /CS SD
      |                 +---------
      |
      |       4.7k      //
     8| ----`/\/\/'---[>|-----+
      |              ERROR    |             +---------
      |                      VSS            |
      |                                     |
      |               +-------------------- |Rx
      |        3.3k   |    2.2k             |     E
    12| -----`/\/\/'--+--`/\/\/'----+       |     S
      |                             |       |     P 
      |                            VSS      |     8
    13| ----------------------------------- |Tx   2
      |        470                          |     6
    11| -----`/\/\/'--------------+-------- |VCC  6
      |                           |         |
------+                           |    +--- |VSS
                +---+             |    |    +---------
                +---+   LM7833    |   VSS
                |3v3|             |
                | | |             |
        VCC ----+ | +-------------+
                  |
                 VSS

*/

// include the SD library
#include <SPI.h>
#include <SD.h>
//#include <SoftwareSerial.h> //Wifi Module

#define SD_CS 9

//Data Lines from the c64
#define ATN 5
#define CLK 6
#define DAT 7

//Error LED Pin
#define ERRLED 8

//Indicates if WIFI module is enabled
//#define WIFI_ENABLED 11

//Device numbers for ARDUINO SD DRIVE and ESP8266 Interface
#define DRIVE_NUMBER 8    //Device #8 is DISK
//#define WIFI_NUMBER 16    //Device #16 is WIFI

#define FALSE 0
#define TRUE 1

bool eoi = FALSE;           //EOI for recieving
bool myeoi = FALSE;         //EOI for sending
byte byteIn = 0x00;         //Current Byte recieved
bool haveGotData = FALSE;   //Did get data yet?
String filename = "";       //String to represent File names
char filenameByte = 0;      //Used in conversion of bytes to ASCII
char fileBuffer = 0;        //Used as a buffer when recieving files from the computer
unsigned long fileSize = 0; //Current character counter for sending files, used to find the EOI of a file
bool fileOverwrite = FALSE; //Determines if a file can be overwritten
bool fileIsDir = FALSE;     //Used to determine if a file is a directory or not, see (load "*",8)
int timeTaken = 0;          //Used in timing a frame handshake when sending bytes from device
byte deviceSelect = 0;      //Device Number selected by the computer
byte openChannel = 0;       //Current channel opened by the computer



/**********************************************
 *          STATUS AND COMMAND CODES          *
 **********************************************/
#define RESET 0
#define ATN_TRUE 1
#define LISTEN 2
#define UNLISTEN 3
#define TALK 4
#define UNTALK 5
#define OPEN_CAN 6
#define CLOSE 7
#define OPEN 8

#define LOAD 9
#define SAVE 10


byte currentStatus = RESET;   //What mode we are in
byte currentCommand = RESET; //Loading or Saving?


/*******************************************
 *          ERROR LED FLASH CODES          *
 *******************************************/
 
#define OVERWRITE_ERROR 2   //Two flashes if a file is attempted to be overwritten without the proper syntax ("@0:")
                            //Use "@0:" infront of the filename to overwrite existing file
#define FRAME_ERROR 3       //Computer did respond with a frame handshake within 1ms

#define SD_ERROR 4          //SD card failed to initialize

byte errorCode = RESET;     //Error codes for LED


File root;        //Used as the root directory when searching for first file on disk
File tempFile;    //Setup tempory file

  
void setup() {
  //start serial connection
  Serial.begin(115200);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }
  
  if (!SD.begin(SD_CS)) {   //Start SD Driver
    Serial.println("SD initialization failed!");
    errorCode = SD_ERROR; //Set flash code and flash it
    flashError(errorCode);
  }
  Serial.println("SD initialization done.");
  //Configure all input pullup resistors
  //These can be used as resistive outputs
  pinMode(ATN, INPUT_PULLUP); 
  digitalWrite(CLK, LOW);
  pinMode(CLK, INPUT);  //Allows it to be pulled low by other devices on the line
  digitalWrite(DAT, LOW);
  pinMode(DAT, INPUT);  //Allows it to be pulled low by other devices on the line

  //Reset Error LED
  pinMode(ERRLED, OUTPUT);
  digitalWrite(ERRLED, LOW);

  
  Serial.println();
  Serial.println("Ready. v3.25");
}

void loop() {
  
  if ((digitalRead(ATN) == LOW) && (currentStatus < OPEN)) {  //Computer is requesting to communicate
                              //&& (digitalRead(CLK) == LOW)
    byte currentByte = 0;
    do {
      pinMode(DAT, OUTPUT);   //Set DATA TRUE
  
      //When ready, continue
  
      Serial.println("ATN");
      currentByte = getByte();
      Serial.println(currentByte, HEX);
      if (currentByte < 0x3f) {   //If recieving a listen command, listen
        deviceSelect = currentByte - 0x20;  //Remove the base address from the command and set it as the device number
        currentStatus = LISTEN;
        currentCommand = SAVE;    //Load a file
      }
      else if (currentByte == 0x3f) {   //UNLISTEN
        currentStatus = UNLISTEN;
      }
      else if (currentByte < 0x4f && currentByte > 0x3f) {  //TALK
        deviceSelect = currentByte - 0x40;  //Remove the base address from the command recieved
        currentStatus = TALK;
        currentCommand = LOAD;    //Save a file
      }
      else if (currentByte == 0x5f) { //UNTALK
        currentStatus = UNTALK;
      }
      else if (currentByte < 0x70 && currentByte >= 0x60) {   //OPEN
        openChannel = currentByte - 0x60;   //Set the current channel
        currentStatus = OPEN_CAN;
      }
      else if (currentByte < 0xf0 && currentByte >= 0xe0) {   //CLOSE
        openChannel = 0;  //Reset current channel
        currentStatus = CLOSE;
      }
      else if (currentByte <= 0xff && currentByte >= 0xf0) {   //OPEN
        openChannel = currentByte - 0x60;   //Set the current channel
        currentStatus = OPEN;
      }
      //Serial.println(currentStatus);
    } while ((digitalRead(ATN) == LOW) and (currentStatus < OPEN));  //Loop until end of commands
  }
  
  else if (currentStatus == OPEN_CAN and deviceSelect == DRIVE_NUMBER) {   //Lets open a file to send to the computer  
     
    if (currentCommand == LOAD) {    //If we are in load mode
      Serial.println("LOAD");
      turnAroundSlave();  //Turn the bus around

      if (filename == "$") {            //User wants to see a directory
        sendDir();    //Send the directory
      }
      else if (filename == "*") {       //User wants to load first file on disk
        root = SD.open("/");            //Open root directory
        root.rewindDirectory();         //Set to first file in directory
        
        do {
          tempFile = root.openNextFile();   //Open first file in the directory
          Serial.println(tempFile.name());  //Print the name
          
          if (tempFile.isDirectory()) {   //If the file opened is a directory, close it
            tempFile.close();             //Close the file
            fileIsDir = TRUE;             //Set flag that the file is a DIRECTORY
          }  else {
            fileIsDir = FALSE;            //Otherwise, the file is actually a file!          
            filename = tempFile.name();   //Get filename
            tempFile.close();             //Close the file becauase it is reopened in the sendFile function
          }
        } while (fileIsDir);  //Loop until file opened is not a directory
        root.close();

        sendFile(filename);             //Send the file
      }
      else if (SD.exists(filename)) {   //Check to see if the file exists on the SD card, otherwise, turn around again to indicate an error suituation
        sendFile(filename);              //Send the file
      }
      
      //If there was no file with that name, this Master turnaround will indicate an error condition, 
      // but if the file was found, it will return the arduino to the listener and the computer to the talker
      turnAroundMaster(); 
    }
    
    else if (currentCommand == SAVE) {
      Serial.println("SAVE");
      if (SD.exists(filename)) {   //Overwrite an existing file
        if (fileOverwrite == TRUE) {  //Yes, the file can be overwritten
          Serial.println("EXISTS");
          SD.remove(filename);    //Delete the existing file
          writeFile(filename);    //Write to the file
          fileOverwrite = FALSE;  //Reset file overwrite enable
          
        } else {    //If trying ot overwrite a file without proper syntax ("@0:"), error
          errorCode = OVERWRITE_ERROR;    //Set error code
          flashError(errorCode);          //Flash error code
        }
      }
      else if (!SD.exists(filename)) {  //Create new file
        Serial.println("DOES NOT EXIST");
        writeFile(filename);    //Create and write to the file
        //tempFile = SD.open(filename, FILE_WRITE); //Put new file on SD
        //tempFile.close();   //Close the file and save changes
      }
    }

    currentStatus = RESET;    //Reset the current Status
    currentCommand = RESET;
  }
            
  else if (currentStatus == OPEN and deviceSelect == DRIVE_NUMBER) { //Get the filename to be opened if the channel is 0 or 1
          
    Serial.println("OPENFile");
    filename = "";                    //Clear filename
    do {                              //Loop until entire filename has been recieved
      filenameByte = getByte();       //Convert the byte to a character
      filename.concat(filenameByte);  //Convert all bytes of the filename to a string
    } while (eoi == FALSE);           //Keep recieving bytes until the entire file name has been recieved (eoi == TRUE)
   
    if (filename.startsWith("@0:")) { //If the filename begins with "@0:" then it is intended to overwrite the existing file
      fileOverwrite = TRUE;           //Set flag to allow overwrites
      filename.remove(0,3);           //Remove the first three characters from the filename string
    }
    Serial.println(filename);
  
    currentStatus = RESET;            //Reset the current Status
  }
  
  else if (currentStatus == UNLISTEN) {
    Serial.println();
    currentStatus = RESET;          //Reset the current Status
  }

}




void sendDir() {      //Creates a directory and sends it
  digitalWrite(ERRLED, 1);
  unsigned int currentOffset = 0x0801;  //Address of BASIC line
  unsigned int lineNumber = 10;         //Line number
  String tempName = "";                 //Temporary file name variable
  String tempSize = "";                 //Stores the size of the file
  unsigned int lineLength = 0;          //Lenght of line
  
  shiftByteOut(lowByte(currentOffset), FALSE);   //Send the currentOffset - this is the first two bytes (little-endian) for the start of a .PRG file
  shiftByteOut(highByte(currentOffset), FALSE); 


  //Output the title line:  "10 FILE         SIZE"
  currentOffset += 0x18;     //Increment current offset to next line of BASIC program
  shiftByteOut(lowByte(currentOffset), FALSE);   //CurrentOffset
  shiftByteOut(highByte(currentOffset), FALSE); 

  shiftByteOut(lowByte(lineNumber), FALSE);   //Send line number
  shiftByteOut(highByte(lineNumber), FALSE); 

  sendStringofBytes(" FILE          SIZE");  //Send file header
  
  shiftByteOut(0x00, FALSE);                  //Send end-of-line marker


  root = SD.open("/");  //Open root
  //root.rewindDirectory(); //Go to first File
  while (true) {      //Keep going until no more files, (Stops with a break)
    File entry = root.openNextFile();
    if (!entry) {
      //That is end of directory
      break;  //Break loop 
    }
    if (!entry.isDirectory()) {   //Only output a directory listing if the file is not a directory
      tempName = entry.name();    //Get the name of the current file
      tempSize = entry.size();    //Get size of file
      lineLength = 20 + tempSize.length(); //Find the lenght of the current line (the 20 is for the extra bytes - next line offset, line number, SPACE at beging of line, filename and followinf spaces, and ending null)
      Serial.println(tempName);
      
      currentOffset += lineLength;     //Increment current offset to next line of BASIC program
      shiftByteOut(lowByte(currentOffset), FALSE);   //CurrentOffset
      shiftByteOut(highByte(currentOffset), FALSE); 

      lineNumber++;
      shiftByteOut(lowByte(lineNumber), FALSE);   //Send line number
      shiftByteOut(highByte(lineNumber), FALSE); 

      shiftByteOut(0x20, FALSE);                   //Add Space at begining of line

      sendStringofBytes(tempName);                //Send filename
      
      for (int spcCntr = 0; spcCntr <= 13-tempName.length(); spcCntr++) { //The 13 is the width, in characters, of the NAME column on the directory
        shiftByteOut(0x20, FALSE);     //Send enough spaces to move position to begining of the SIZE column on screen
      }
      sendStringofBytes(tempSize);
      
      shiftByteOut(0x00, FALSE);                  //Send end-of-line marker
    }
    entry.close();                                //Close currently open file
  }
  shiftByteOut(0x00, FALSE);   //Send the end-of-file indicator, both the 00 00 and the EOI
  shiftByteOut(0x00, TRUE);
  root.close();                 //Close root directory
  digitalWrite(ERRLED, 0);
}

void sendStringofBytes(String tempString) {   //Send a string to the computer (c64)
                                              //DOES NOT SEND AND EOI!!!!!!
  for (int strPrt = 0; strPrt < tempString.length(); strPrt++) {   //Itterate byte by byte until string is done
    shiftByteOut(tempString.charAt(strPrt), FALSE);   //Send the character
  }
}

void sendFile(String tempName) {
  digitalWrite(ERRLED, 1);
  tempFile = SD.open(tempName);   //Open selected file
  if (tempFile) {   //Hopefully, the file opened correctly, if not, exit if statements and return an error
    fileSize = tempFile.size() - 1; //Ge the size of the file minus one to know when to signal EOI
    Serial.println(fileSize);
    while (tempFile.available()) {      //Read until end of file
      if (fileSize == 0) {      //Check is EOI is needed
        myeoi = TRUE;                   //Signal that this is the last character
      }
      shiftByteOut(tempFile.read(), myeoi);  //Read byte from file then send it
      fileSize--;             //Increment the character counter
    }
    tempFile.close();         //Close the file
  }
  myeoi = FALSE;              //Reset EOI indicator
  digitalWrite(ERRLED, 0);
}

void writeFile(String tempFilename) {
  digitalWrite(ERRLED, 1);
  tempFile = SD.open(tempFilename, FILE_WRITE);   //Open the selected file
  
  fileBuffer = "";                //Clear filebuffer
  
  do {                            //Loop until entire file has been recieved
    fileBuffer = getByte();       //Convert the byte to a character
    tempFile.print(fileBuffer);   //Send character to file
  } while (eoi == FALSE);         //Keep recieving bytes until the entire file has been recieved (eoi == TRUE)             
  tempFile.close();               //Save the file and close it
  digitalWrite(ERRLED, 0);
}   

void turnAroundSlave() {    //Set DEVICE as Talker
  while (digitalRead(CLK) == LOW) { //Wait for computer to release the Clock Line
  }
  pinMode(DAT, INPUT);  //Release the Data line
  pinMode(CLK, OUTPUT); //Pull Clock true for starting position
}

void turnAroundMaster() {   //Set COMPUTER as Talker
  pinMode(DAT, OUTPUT);   //Pull the data true
  pinMode(CLK, INPUT);    //Release Clock 
}

byte getByte() {
  eoi = FALSE;  //Clear EOI flag
  pinMode(DAT, OUTPUT); //----------------------------------
  while (digitalRead(CLK) == LOW) {
  } //Wait for the computer to request to send a byte

  pinMode(DAT, INPUT);  //Release DATA line

  byte delayValue = 0;
  byte eoi_delay = 0;
  while (digitalRead(CLK) == HIGH && eoi_delay < 20) {    //Loop until Computer sets Clock line
    delayMicroseconds(10);
    eoi_delay++;    //Time it by this loop (1 cycle ~= 10 uS)
  }
  if (eoi_delay < 20) {
    byteIn = shiftByteIn();
  }
  else {
    eoi = TRUE;   //Set flag that this is an EOI
    pinMode(DAT, OUTPUT);   //Set DATA to true to signal that 
    delayMicroseconds(70);  //Allow computer to catch Acknowledge
    pinMode(DAT, INPUT);
    byteIn = shiftByteIn();
  }
  Serial.println(eoi);
  delayMicroseconds(20);
  pinMode(DAT, OUTPUT);  //Frame Handshake Acknowledge
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

byte shiftByteOut(byte byteOut, bool iseoi) {  
  pinMode(CLK, OUTPUT);
  delayMicroseconds(60);
  pinMode(CLK, INPUT);    //Release Clock line to say "Ready to send Byte"

  while (digitalRead(DAT) == LOW) {   //Wait for listner to be ready
  }

  if (iseoi == FALSE) {
    delayMicroseconds(30);  //Not an EOI
  } else {
    delayMicroseconds(250); //Is an EOI
    while (digitalRead(DAT) == HIGH) {  //Wait for listner to acknowledge EOI
    }
    while (digitalRead(DAT) == LOW) {
    }
  }

  pinMode(CLK, OUTPUT);   //Say, "I am transmitting byte"
  delayMicroseconds(10);

  Serial.println(byteOut, HEX);
  
  for (int i=0; i<=7; i++) {
    if (byteOut & (1 << i)) { //Get bit and set the data line accordingly
      pinMode(DAT, INPUT);  //1
    } else {
      pinMode(DAT, OUTPUT); //0
    }
    
    delayMicroseconds(70);  //Bit setup time
    pinMode(CLK, INPUT);    //Say bit is set

    delayMicroseconds(60);  //Allow computer to see bit
    pinMode(CLK, OUTPUT);
    pinMode(DAT, INPUT);
  }

  timeTaken = 0;  //Reset the frame handshake counter
  while (digitalRead(DAT) == HIGH && timeTaken <= 1000) {  //Wait for listner to pull DATA TRUE
    timeTaken++;
  }                                   // Frame Handshake
  
  if (timeTaken > 1000) {     //The computer took longer than 1ms to acknoledge byte frame
    errorCode = FRAME_ERROR;  //Set error code
    flashError(errorCode);             //Flash code
  }
}


void flashError(byte errorCodeIn) {    //Error status, locks arduino and requires a reset
  pinMode(DAT, INPUT);
  pinMode(CLK, INPUT);
  bool stat = 1;
  while (true) {      //Flash ERROR LED (with error code), forever... (or as long as "true" is true :)
    for (byte e = 0; e <= 2*errorCodeIn-1; e++){
      digitalWrite(ERRLED, stat);
      stat = !stat;
      delay(200);
    }
    
    delay(500);
  }
}


