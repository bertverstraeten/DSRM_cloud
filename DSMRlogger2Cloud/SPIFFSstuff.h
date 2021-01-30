/*
***************************************************************************  
**  Program  : SPIFFSstuff, part of DSMRlogger2HTTP
**  Version  : v0.7.7
**
**  Copyright (c) 2019 Willem Aandewiel
**
**  TERMS OF USE: MIT License. See bottom of file.                                                            
***************************************************************************      
*/

//===========================================================================================
int32_t freeSpace() {
//===========================================================================================
  int32_t space;
  
  SPIFFS.info(SPIFFSinfo);

  space = (int32_t)(SPIFFSinfo.totalBytes - SPIFFSinfo.usedBytes);

  return space;
  
} // freeSpace()

//===========================================================================================
void listSPIFFS() {
//===========================================================================================
  Dir dir = SPIFFS.openDir("/");

  TelnetStream.println();
  while (dir.next()) {
    File f = dir.openFile("r");
    TelnetStream.printf("%-15s %ld \r\n", dir.fileName().c_str(), f.size());
    yield();
  }
  TelnetStream.flush();

  SPIFFS.info(SPIFFSinfo);

  int32_t space = (int32_t)(SPIFFSinfo.totalBytes - SPIFFSinfo.usedBytes);
  TelnetStream.println("\r");
  TelnetStream.printf("Available SPIFFS space [%ld]bytes\r\n", freeSpace());
  TelnetStream.printf("           SPIFFS Size [%ld]kB\r\n", (SPIFFSinfo.totalBytes / 1024));
  TelnetStream.printf("     SPIFFS block Size [%ld]bytes\r\n", SPIFFSinfo.blockSize);
  TelnetStream.printf("      SPIFFS page Size [%ld]bytes\r\n", SPIFFSinfo.pageSize);
  TelnetStream.printf(" SPIFFS max.Open Files [%ld]\r\n\n", SPIFFSinfo.maxOpenFiles);


} // listSPIFFS()

/***************************************************************************
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to permit
* persons to whom the Software is furnished to do so, subject to the
* following conditions:
*
* The above copyright notice and this permission notice shall be included
* in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
* OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
* OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
* THE USE OR OTHER DEALINGS IN THE SOFTWARE.
* 
***************************************************************************/
