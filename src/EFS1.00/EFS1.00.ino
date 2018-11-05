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
      |              ERROR    |             
      |                      VSS            
------+
*/

// include the SD library
#include <SPI.h>
#include <SD.h>

#define SD_CS 9

//Data Lines from the c64
#define ATN 5
#define CLK 6
#define DAT 7

//Error LED Pin
#define ERRLED 8

//Device numbers for ARDUINO SD DRIVE and ESP8266 Interface
#define DRIVE_NUMBER 8    //Device #8 is DISK

#define FALSE 0
#define TRUE 1

bool eoi = FALSE;           //EOI for recieving
bool myeoi = FALSE;         //EOI for sending
byte byteIn = 0x00;         //Current Byte recieved
bool haveGotData = FALSE;   //Did get data yet?
String filename = "";       //String to represent File names
String filenameCon = "";    //Filename used as temporary buffer in locating the filename in the dir file
char fileByte = 0;          //Used in parsing the directory file
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
#define FRAME_ERROR 3       //Computer did not respond with a frame handshake within 1ms

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
  if (!SD.exists("dir")) {
    //If the dir file is not on the card, auto-generate one
    Serial.println("dir NOT FOUND! Generating a new one...");
    File dir = SD.open("/dir", FILE_WRITE);
    dir.print("#SD dir file auto generated\r#Line-endings are macintosh-format (CR)\r#\r#Format:\r#[FILENAME EXPECTED BY C64]`[ACTUAL FILENAME ON DISK];");
    dir.close();
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
  Serial.println("Ready. vEFS1.00");
}

void loop() {
  
  if ((digitalRead(ATN) == LOW) && (currentStatus < OPEN)) {  //Computer is requesting to communicate
                              //&& (digitalRead(CLK) == LOW)
    byte currentByte = 0;
    do {
      Serial.println("ATN");
      currentByte = getByte(); //Sets DATA TRUE
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
        root = SD.open("/dir");            //Open root directory
           
        do {      
          fileByte = root.read();
          if (fileByte == '#') {  //Skip Comments
            while (root.read() != 0x0d)  //Loop until end of line
              if (root.position() == root.size())
                break;  //Prevent the arduino from being stuck in an infinite loop!
          }
          else if (fileByte == 0x0d) {
            
          }
          else {
            while (root.read() != '`') {
              //Loop until the first file is found
              if (root.position() == root.size())
                break;  //Prevent the arduino from being stuck in an infinite loop!
            }

            filename = "";  //Clear the filename given by the c64
            //Replace filename by the file name in the dir file.
            while (root.peek() != ';') {
              filenameByte = root.read();
              filename.concat(filenameByte);
              if (root.position() == root.size())
                break;  //Prevent the arduino from being stuck in an infinite loop!
              //Serial.println(filenameByte);
            } 
            break;  //Stop checking the dir file table
          } 
          
        } while (root.size()-1 > root.position());  //Loop until end of directory file
        root.close();

        sendFile(filename);             //Send the file
      }
      else {    //Check to see if the file exists on the SD card, otherwise, turn around again to indicate an error suituation
                //Get the filename from the dir file and send the file
        sendFile(getFilenameFromDir(filename)); 
      }
      
      //If there was no file with that name, this Master turnaround will indicate an error condition, 
      // but if the file was found, it will return the arduino to the listener and the computer to the talker
      turnAroundMaster(); 
    }
    
    else if (currentCommand == SAVE) {
      Serial.println("SAVE");
      filenameCon = getFilenameFromDir(filename);
      Serial.println(filenameCon);
      if (SD.exists(filenameCon)) {   //Overwrite an existing file
        if (fileOverwrite == TRUE) {  //Yes, the file can be overwritten
          Serial.println("EXISTS");
          SD.remove(filenameCon);    //Delete the existing file
          writeFile(filenameCon);    //Write to the file
          fileOverwrite = FALSE;  //Reset file overwrite enable
          
        } else {    //If trying to overwrite a file without proper syntax ("@0:"), error
          errorCode = OVERWRITE_ERROR;    //Set error code
          flashError(errorCode);          //Flash error code
        }
      }
      else if (!SD.exists(filenameCon)) {  //Create new file
        Serial.println("DOES NOT EXIST");
 
        writeFile(createDirEntry(filename)); //Create and write to the file
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

String createDirEntry(String tempName) {    //Creates a new entry in dir file
  root = SD.open("dir", FILE_WRITE);            //Open root directory
  root.seek(root.size()); //Go to end of file
  //Insert the entry into the dir table
  root.write(0x0d);
  root.print(tempName);
  root.print('`');
  root.print(tempName.substring(0,8));  //Use only the first 8 characters of the filename as the actual filename
  root.print(';');
  root.close();   //Close 'dir
  return tempName.substring(0,8);   //Return the name of the file on disk
}
String getFilenameFromDir(String tempName) {
  root = SD.open("/dir");            //Open root directory
           
  do {      
    fileByte = root.read();
    if (fileByte == '#') {  //Skip Comments
      while (root.read() != 0x0d) //Loop until end of line
        if (root.position() == root.size())
          break;  //Prevent the arduino from being stuck in an infinite loop!
    }
    else if (fileByte == 0x0d) {
      
    }
    else {
      filenameCon = "";  
      root.seek(root.position()-1); //Backup one character in the file
      //Loop to check the filename in the next entry
      while (root.peek() != '`') {
        filenameByte = root.read();
        filenameCon.concat(filenameByte);
        if (root.position() == root.size())
          break;  //Prevent the arduino from being stuck in an infinite loop!
        //Serial.println(filenameByte);
      } 
      filenameCon.toUpperCase();
      Serial.println(filenameCon);
      if (filenameCon != tempName) {
        if (root.position() == root.size())
          break;  //End of file means no more entries in the file!
        while (root.read() != 0x0d) { //Loop until end of line
          if (root.position() == root.size()) {  //Make sure not at end of file
            tempName = "FILENOTF.ONF";  //ERR: File not found!
            break;  //Done!
          }
        }
        continue; //If not the corect file, check again
      }
  
      root.read();
      tempName = "";  //Clear the filename given by the c64
      //Replace filename by the file name in the dir file.
      while (root.peek() != ';') {
        filenameByte = root.read();
        tempName.concat(filenameByte);
        //Serial.println(filenameByte);
      } 
      break;  //Stop checking the dir file table
    } 
  } while (root.position() != root.size());  //Loop until end of directory file

  root.close();   //Close 'dir'

  return tempName;
}

void sendDir() {      //Creates a directory and sends it
  digitalWrite(ERRLED, 1);
  unsigned int currentOffset = 0x0801;  //Address of BASIC line
  unsigned int lineNumber = 10;         //Line number
  String tempName = "";                 //Temporary file name variable
  String tempSize = "";                 //Stores the size of the file
  unsigned int lineLength = 0;          //Lenght of line
  unsigned int seekPos = 0;             //Last seek position in file before closing and opening the other file to get size
  
  shiftByteOut(lowByte(currentOffset), FALSE);   //Send the currentOffset - this is the first two bytes (little-endian) for the start of a .PRG file
  shiftByteOut(highByte(currentOffset), FALSE); 


  //Output the title line:  "10 FILE                SIZE"
  currentOffset += 0x1F;     //Increment current offset to next line of BASIC program
  shiftByteOut(lowByte(currentOffset), FALSE);   //CurrentOffset
  shiftByteOut(highByte(currentOffset), FALSE); 

  shiftByteOut(lowByte(lineNumber), FALSE);   //Send line number
  shiftByteOut(highByte(lineNumber), FALSE); 

  sendStringofBytes(" FILE                 SIZE");  //Send file header
  
  shiftByteOut(0x00, FALSE);                  //Send end-of-line marker

  root = SD.open("/dir"); //Open root directory
           
  do {      
    fileByte = root.read();
    if (fileByte == '#') {  //Skip Comments
      while (root.read() != 0x0d) //Loop until end of line
        if (root.position() == root.size())
          break;  //Prevent the arduino from being stuck in an infinite loop!
    }
    else if (fileByte == 0x0d) {  //Skip line ending
      
    }
    else {
      tempName = "";  
      root.seek(root.position()-1); //Backup one character in the file
      if (root.peek() == ';')
        root.seek(root.position()+2); //Cuts off ';\r' from filename
      //Loop to check the filename in the next entry
      while (root.peek() != '`') {
        filenameByte = root.read();
        tempName.concat(filenameByte);
        if (root.position() == root.size())
          break;  //Prevent the arduino from being stuck in an infinite loop!
        //Serial.println(filenameByte);
      } 
      
  
      root.read();
      filenameCon = "";  //Clear the filename given by the c64
      //Replace filename by the file name in the dir file.
      while (root.peek() != ';') {
        filenameByte = root.read();
        filenameCon.concat(filenameByte);
        if (root.position() == root.size())
          break;  //Prevent the arduino from being stuck in an infinite loop!
        //Serial.println(filenameByte);
      } 
      seekPos = root.position();  //Save current search position
      root.close();               //Close the dir file
      File entry = SD.open(filenameCon);  //Open the file to get filesize
      tempSize = entry.size();              //Get the size of current file
      currentOffset += 27 + tempSize.length();   //Find the length of the current line and add it to the current offset (The 20 is for the unber of extra bytes - next line offset, linenumber, SPACE at beginning of line, filename and following spaces, and ending NULL)

      shiftByteOut(lowByte(currentOffset), FALSE);  //Send current offset
      shiftByteOut(highByte(currentOffset), FALSE);

      lineNumber++;
      shiftByteOut(lowByte(lineNumber), FALSE);  //Send line number
      shiftByteOut(highByte(lineNumber), FALSE);


      shiftByteOut(0x20, FALSE);    // Add space at beginning of line

      tempName.toUpperCase();
      sendStringofBytes(tempName);  //Send Filename

      for (int spcCntr = 0; spcCntr <= 20-tempName.length(); spcCntr++) { //The 20 is the width, in characters, of the NAME column on the directory
        shiftByteOut(0x20, FALSE);    //Send enough spaces to move position to beginning of the SIZE column on screen
      }
      sendStringofBytes(tempSize);

      shiftByteOut(0x00, FALSE);      //Send end-of-line marker
      entry.close();                  //Close the currently open file
      root = SD.open("/dir");         //Re-open the dir file
      root.seek(seekPos);             //Return to original seek position
    } 
  } while (root.position() < root.size()-1);  //Loop until end of directory file

  root.close();   //Close 'dir'

  shiftByteOut(0x00, FALSE);   //Send the end-of-file indicator, both the 00 00 and the EOI
  shiftByteOut(0x00, TRUE);

  digitalWrite(ERRLED, 0);
}

void sendStringofBytes(String tempString) {   //Send a string to the computer (c64)
                                              //DOES NOT SEND AND EOI!!!!!!
  for (int strPrt = 0; strPrt < tempString.length(); strPrt++) {   //Itterate byte by byte until string is done
    shiftByteOut(tempString.charAt(strPrt), FALSE);   //Send the character
  }
}

void sendFile(String tempName) {
  Serial.println(tempName);
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


