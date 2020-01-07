# 1541EmU
Source code and eagle files for the 1541 Emulator
<hr>

 _Description_
 
  The 1541EmU is a 1541 emulator for the c64. It is based on the ATMEGA328P-PU and interfaces over the IEC bus. Files are stored on a micro SD card formatted to FAT32 using the 8.3 file name format. Included is a drive reset button that resets the drive, but not the computer. There is also a solder jumper to enable reset-by-computer, which allows the c64's reset line to reset the drive.
 <br> 
 I have a demo <a href="https://youtu.be/4uivsbCX4fI">here</a>. (https://youtu.be/4uivsbCX4fI)
  
 _Features_
 
  * Save and load files with almost any filename (ASCII Characters only)
  * Change file order on disk (Through a computer)
  * <code>LOAD "*",8</code> is allowed
  * View directory
  * Overwrite existing files on disk
  * Auto initialization of filename translation file (DIR)
  * Cassette interface pass through connector.
  * ICSP and Tx/Rx headers
  * Power and status LEDs
  * Compatible with the Arduino IDE
  
  
# Board
Latest board revision: 1.1 on September 9, 2018
  (Note: there is no official revision 1.0)
  
  1) Checked schematic, part numbers, and part footprint sizing.
  2) Fixed board dimension issue
  
# Source
Latest revision: EFS1.00 (Extended File System v1.00) on October 23, 2018
  
  1) Added the "DIR" file that translates the expected filename from the c64 to the actual name that is stored on the SD
  2) Fixed "DEVICE NOT PRESENT ERROR" (as far as I can test the drive)
  
  
  
