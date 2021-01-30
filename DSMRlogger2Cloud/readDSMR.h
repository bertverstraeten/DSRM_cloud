void UpdateElectricity()
{
  char sValue[255];
  sprintf(sValue, "%d;%d;%d;%d;%d;%d", mEVLT, mEVHT, mEOLT, mEOHT, mEAV, mEAT);
  Serial.println("");
  Serial.print("Verbruik tarfief 1 = ");Serial.print(mEVLT);Serial.print(" ");
  Serial.print("Verbruik tarfief 2 = ");Serial.print(mEVHT);Serial.print(" ");
  Serial.print("Opbrengst tarfief 1 = ");Serial.print(mEOLT);Serial.print(" ");
  Serial.print("Opbrengst tarfief 2 = ");Serial.print(mEOHT);Serial.print(" ");
  Serial.print("Actueel verbruik = ");Serial.print(mEAV);Serial.print(" ");
  Serial.print("Actuele opbrengst = ");Serial.print(mEAT);Serial.print(" ");
}


bool isNumber(char* res, int len) {
  for (int i = 0; i < len; i++) {
    if (((res[i] < '0') || (res[i] > '9'))  && (res[i] != '.' && res[i] != 0)) {
      return false;
    }
  }
  return true;
}

int FindCharInArrayRev(char array[], char c, int len) {
  for (int i = len - 1; i >= 0; i--) {
    if (array[i] == c) {
      return i;
    }
  }
  return -1;
}

long getValidVal(long valNew, long valOld, long maxDiffer)
{
  //check if the incoming value is valid
      if(valOld > 0 && ((valNew - valOld > maxDiffer) && (valOld - valNew > maxDiffer)))
        return valOld;
      return valNew;
}

long getValue(char* buffer, int maxlen) {
  int s = FindCharInArrayRev(buffer, '(', maxlen - 2);
  if (s < 8) return 0;
  if (s > 32) s = 32;
  int l = FindCharInArrayRev(buffer, '*', maxlen - 2) - s - 1;
  if (l < 4) return 0;
  if (l > 12) return 0;
  char res[16];
  memset(res, 0, sizeof(res));

  if (strncpy(res, buffer + s + 1, l)) {
    if (isNumber(res, l)) {
      return (1000 * atof(res));
    }
  }
  return 0;
}

bool decodeTelegram(int len) {
  //need to check for start
  int startChar = FindCharInArrayRev(telegram, '/', len);
  int endChar = FindCharInArrayRev(telegram, '!', len);
  bool validCRCFound = false;
  if(startChar>=0)
  {
    //start found. Reset CRC calculation
    currentCRC=CRC16(0x0000,(unsigned char *) telegram+startChar, len-startChar);
    if(outputOnSerial)
    {
//      for(int cnt=startChar; cnt<len-startChar;cnt++)
        //Serial.print(telegram[cnt]);
    }    
    //Serial.println("Start found!");
    
  }
  else if(endChar>=0)
  {
    //add to crc calc 
    currentCRC=CRC16(currentCRC,(unsigned char*)telegram+endChar, 1);
    char messageCRC[5];
    strncpy(messageCRC, telegram + endChar + 1, 4);
    messageCRC[4]=0; //thanks to HarmOtten (issue 5)
    if(outputOnSerial)
    {
      for(int cnt=0; cnt<len;cnt++)
        ;//Serial.print(telegram[cnt]);
    }    
   validCRCFound = (strtol(messageCRC, NULL, 16) == currentCRC);
    if(validCRCFound)
      ;//Serial.println("\nVALID CRC FOUND!"); 
    else
     ;// Serial.println("\n===INVALID CRC FOUND!===");
    currentCRC = 0;
  }
  else
  {
    currentCRC=CRC16(currentCRC, (unsigned char*)telegram, len);
    if(outputOnSerial)
    {
      for(int cnt=0; cnt<len;cnt++)
        ;//Serial.print(telegram[cnt]);
    }
  }

  long val =0;
  long val2=0;
  //Serial.println("");
  //Serial.print("Telegram =");
  //Serial.println(telegram); 
  
  // 1-0:1.8.1(000992.992*kWh)
  // 1-0:1.8.1 = Elektra verbruik laag tarief (DSMR v4.0)
  if (strncmp(telegram, "1-0:1.8.1", strlen("1-0:1.8.1")) == 0){ 
    mEVLT_temp =  getValue(telegram, len);
    if (mEVLT_temp > 0.1) {
      mEVLT = mEVLT_temp; // remove fault readings
    }
  }
  

  // 1-0:1.8.2(000560.157*kWh)
  // 1-0:1.8.2 = Elektra verbruik hoog tarief (DSMR v4.0)
  if (strncmp(telegram, "1-0:1.8.2", strlen("1-0:1.8.2")) == 0){ 
    mEVHT_temp = getValue(telegram, len);
    if (mEVHT_temp > 0.1) {
      mEVHT = mEVHT_temp; // remove fault readings
    }
    }
    
  // 1-0:2.8.1(003563.888*kWh)
  // 1-0:2.8.1(000348.890*kWh)
  // 1-0:2.8.1 = Elektra opbrengst laag tarief (DSMR v4.0)
  if (strncmp(telegram, "1-0:2.8.1", strlen("1-0:2.8.1")) == 0) {
    mEOLT_temp = getValue(telegram, len);
    if (mEOLT_temp > 0.1) {
      mEOLT = mEOLT_temp; // remove fault readings
    }
    }
   

  // 1-0:2.8.2(000859.885*kWh)
  // 1-0:2.8.2 = Elektra opbrengst hoog tarief (DSMR v4.0)
  if (strncmp(telegram, "1-0:2.8.2", strlen("1-0:2.8.2")) == 0) {
    mEOHT_temp = getValue(telegram, len);
    if (mEOHT_temp > 0.1) {
      mEOHT = mEOHT_temp; // remove fault readings
    }
  }

  // 1-0:1.7.0(00.424*kW) Actueel verbruik
  // 1-0:2.7.0(00.000*kW) Actuele teruglevering
  // 1-0:1.7.x = Electricity consumption actual usage (DSMR v4.0)
  if (strncmp(telegram, "1-0:1.7.0", strlen("1-0:1.7.0")) == 0) {
    mEAV_temp = getValue(telegram, len);
  }
    
  if (strncmp(telegram, "1-0:2.7.0", strlen("1-0:2.7.0")) == 0){
    mEAT_temp = getValue(telegram, len);
  }
  //filter out bad readings
  if (mEAV_temp+mEAT_temp < 9000 && mEAV_temp + mEAT_temp > 0.1){
    mEAV = mEAV_temp;
    mEAT = mEAT_temp;
  }
  return validCRCFound;
}

void readTelegram() {
  if (mySerial.available()) {
    //Serial.println("telegram available");
    //memset(telegram, 0, sizeof(telegram));
    while (mySerial.available()) {
      //Serial.println("myserial available");
      int len = mySerial.readBytesUntil('\n', telegram, MAXLINELENGTH);
      telegram[len] = '\n';
      telegram[len+1] = 0;
      yield();
      if(decodeTelegram(len+1))
      {
        //Serial.println("decoded");
         UpdateElectricity();
      }
    } // end while 
 }else{;}//Serial.println("No telegram available");}
}
