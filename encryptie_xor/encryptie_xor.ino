char* key = "testdertest";
double data = 1234.56789;
char datachar[7];



void setup() {

  // put your setup code here, to run once:
  Serial.begin(115200);
  
}

void loop() {

  //Convert double to character
  dtostrf(data,6, 5, datachar);
  
  Serial.print("Data to encrypt = "); Serial.println(datachar);
  char* encrypted = XORENC(datachar,key);
  Serial.println("Encrypted data = "); Serial.println(encrypted);
  char* decrypted = XORENC(encrypted,key);
  Serial.println("Decrypted data = "); Serial.println(decrypted);
  
  delay(5000);
}

char* XORENC(char* in, char* key){
  // Brad @ pingturtle.com
  int insize = strlen(in);
  int keysize = strlen(key);
  for(int x=0; x<insize; x++){
    for(int i=0; i<keysize;i++){
      in[x]=(in[x]^key[i])^(x*i);
    }
  }
  return in;
}
