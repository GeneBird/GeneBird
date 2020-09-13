//***************************************
//  WiFi Controlled Quad
//  ESP8266
// By yours truly Muhammad Noor ul Haq
// Pakistan
//
//**************************************
#include <ESP8266WiFi.h>
#include <WebSocketsServer.h>
#include <DNSServer.h>
#include <Wire.h>
#include <Hash.h>
#include "MSP.h"
#include <EEPROM.h>
#include "config.h"
#include <Arduino.h>

volatile unsigned long next;
volatile unsigned int ppm_running=1;

//=================================================
//********************Variables
//=================================================
int ppm[CHANNEL_NUMBER];
byte lowByte, highByte, type, gyro_address, error, clockspeed_ok;
byte roll_axis, pitch_axis, yaw_axis;
byte receiver_check_byte, gyro_check_byte;
int address, cal_int;
unsigned long timer, timer_1, timer_2, timer_3, timer_4, current_time;
float gyro_pitch, gyro_roll, gyro_yaw;
float gyro_roll_cal, gyro_pitch_cal, gyro_yaw_cal;
const byte captive_portal=0;
const byte DNS_PORT = 53;
const char* serverIndex = "<form method='POST' action='/upload' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>";

extern const char index_html[];

unsigned int alivecount=0;

MSP msp;

IPAddress apIP(192, 168, 1, 1);
DNSServer dnsServer;
//ESP8266WebServer server = ESP8266WebServer(80);
ESP8266WebServer server (80);
WebSocketsServer webSocket = WebSocketsServer(81);

void inline ppmISR(void){
  static boolean state = true;

  if (state) {  //start pulse
    digitalWrite(sigPin, onState);
    next = next + (PULSE_LENGTH * CPU_MHZ);
    state = false;
    alivecount++;
  } 
  else{  //end pulse and calculate when to start the next pulse
    static byte cur_chan_numb;
    static unsigned int calc_rest;
  
    digitalWrite(sigPin, !onState);
    state = true;

    if(cur_chan_numb >= CHANNEL_NUMBER){
      cur_chan_numb = 0;
      calc_rest = calc_rest + PULSE_LENGTH;// 
      next = next + ((FRAME_LENGTH - calc_rest) * CPU_MHZ);
      calc_rest = 0;
      digitalWrite(DEBUGPIN, !digitalRead(DEBUGPIN));
    }
    else{
      next = next + ((ppm[cur_chan_numb] - PULSE_LENGTH) * CPU_MHZ);
      calc_rest = calc_rest + ppm[cur_chan_numb];
      cur_chan_numb++;
    }     
  }
  timer0_write(next);
}

void handleRoot() {
   if(ppm_running==0)
  {
    noInterrupts();
    timer0_isr_init();
    timer0_attachInterrupt(ppmISR);
    next=ESP.getCycleCount()+1000;
    timer0_write(next);
    for(int i=0; i<CHANNEL_NUMBER; i++){
      ppm[i]= CHANNEL_DEFAULT_VALUE;
    }
    ppm_running=1;
    interrupts();
  }
  server.send_P(200,"text/html", index_html);
 }
 
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {

    switch(type) {
        case WStype_DISCONNECTED:
            ppm[0]=1100; ppm[1]=1500; ppm[2]=1500; ppm[3]=1500;
            ppm[4]=1100; ppm[5]=1100; ppm[6]=1100; ppm[7]=1100;
             break;
        case WStype_CONNECTED: {
            // send message to client
            webSocket.sendTXT(num, "Connected");
            ppm[0]=1100; ppm[1]=1500; ppm[2]=1500; ppm[3]=1500;
            ppm[4]=1100; ppm[5]=1100; ppm[6]=1100; ppm[7]=1100;
        }
            break;
        case WStype_TEXT: {
            if(payload[0]=='g' && payload[1]=='e' && payload[2]=='t'){
/*              msp_analog_t analogdata;
              String senddata="{\"vbat\": ";
              if (msp.request(MSP_ANALOG, &analogdata, sizeof(analogdata))) {
                senddata+=String(analogdata.vbat);
              }
              else
                senddata+="0";
              senddata += "}";
              webSocket.sendTXT(num, senddata);
*/            }
        }
        break;
        case WStype_BIN: {
          ppm[payload[0]]=(payload[1]<<8)+payload[2];
          alivecount=0;
        }
        break;
    }

}

void setup() {
  Serial.begin(57600); //Serial Monitor Display

  pinMode(Motor1,OUTPUT); //Initialing PWM output for Motor Control Pins 
  pinMode(Motor2,OUTPUT); //Initialing PWM output for Motor Control Pins
  pinMode(Motor3,OUTPUT); //Initialing PWM output for Motor Control Pins
  pinMode(Motor4,OUTPUT); //Initialing PWM output for Motor Control Pins

  analogWrite(Motor1,0); //Giving a 0 signal to stop all motors
  analogWrite(Motor2,0); //Giving a 0 signal to stop all motors
  analogWrite(Motor3,0); //Giving a 0 signal to stop all motors
  analogWrite(Motor4,0); //Giving a 0 signal to stop all motors

  pinMode(Battery_Connection, INPUT_ANALOG); // Initializing Battery Connection Pin for Voltage Measurement
  pinMode(LED, OUTPUT); //Initialing LED 

  Wire.begin(SDA,SCL, I2C_Speed);          //I2C Initiliazination

  delay(250);

//  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));



  WiFi.softAP(ssid,password,2);

  IPAddress myIP = WiFi.softAPIP();

  if(captive_portal)
    dnsServer.start(DNS_PORT, "*", apIP);
  server.onNotFound(handleRoot);
  server.on("/", handleRoot);

  server.on("/update", HTTP_GET, [](){

    noInterrupts();
    timer0_detachInterrupt();
    ppm_running=0;
    interrupts();
    delay(500);    
    server.sendHeader("Connection", "close");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/html", serverIndex);
  });

  server.on("/upload", HTTP_POST, [](){
    server.sendHeader("Connection", "close");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", (Update.hasError())?"FAIL":"OK");
    ESP.restart();
  },[](){
      HTTPUpload& upload = server.upload();
      if(upload.status == UPLOAD_FILE_START){
        WiFiUDP::stopAll();
        uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        if(!Update.begin(maxSketchSpace)){//start with max available size
 //         Update.printError(Serial);
        }
      } 
      else if(upload.status == UPLOAD_FILE_WRITE){
        if(Update.write(upload.buf, upload.currentSize) != upload.currentSize){
//          Update.printError(Serial);
        }
      } 
      else if(upload.status == UPLOAD_FILE_END){
        if(Update.end(true)){ //true to set the size to the current progress
          Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
        } else {
//        Update.printError(Serial);
        }
        Serial.setDebugOutput(false);
      }
      yield();
    });
 
  server.begin();
  webSocket.onEvent(webSocketEvent);
  webSocket.begin();

  noInterrupts();
  timer0_isr_init();
  timer0_attachInterrupt(ppmISR);
  next=ESP.getCycleCount()+1000;
  timer0_write(next);
  for(int i=0; i<CHANNEL_NUMBER; i++){
    ppm[i]= CHANNEL_DEFAULT_VALUE;
  }
  interrupts();
  Serial.begin(115200);
  msp.begin(Serial);
}

unsigned long time_now = 0;

void loop() {
Serial.println("Welcome");
  
  Serial.println(F(""));
  Serial.println(F("==================================================="));
  Serial.println(F("System check"));
  Serial.println(F("==================================================="));
  delay(1000);
  Serial.println(F("Checking I2C clock speed."));
  delay(1000);
  
  TWBR = 12;                      //Set the I2C clock speed to 400kHz.
  
  #if F_CPU == 16000000L          //If the clock speed is 16MHz include the next code line when compiling
    clockspeed_ok = 1;            //Set clockspeed_ok to 1
  #endif                          //End of if statement

  if(TWBR == 12 && clockspeed_ok){
    Serial.println(F("I2C clock speed is correctly set to 400kHz."));
  }
  else{
    Serial.println(F("I2C clock speed is not set to 400kHz. (ERROR 1)"));
    error = 1;
  }
  
  if(error == 0){
    Serial.println(F(""));
    Serial.println(F("==================================================="));
    Serial.println(F("Transmitter setup"));
    Serial.println(F("==================================================="));
    delay(1000);
    Serial.print(F("Checking for valid receiver signals."));
    //Wait 10 seconds until all receiver inputs are valid
    wait_for_receiver();
    Serial.println(F(""));
  }

  //Quit the program in case of an error
  if(error == 0){
    delay(2000);
    Serial.println(F("Place all sticks and subtrims in the center position within 10 seconds."));
    for(int i = 9;i > 0;i--){
      delay(1000);
      Serial.print(i);
      Serial.print(" ");
    }
    Serial.println(" ");
    //Store the central stick positions
    center_channel_1 = ppm[0];
    center_channel_2 = ppm[1];
    center_channel_3 = ppm[2];
    center_channel_4 = ppm[3];
    Serial.println(F(""));
    Serial.println(F("Center positions stored."));
    Serial.print(F("1st Channel"));
    Serial.println(ppm[0]);
    Serial.print(F("2nd Channel "));
    Serial.println(ppm[1]);
    Serial.print(F("3rd Channel "));
    Serial.println(ppm[2]);
    Serial.print(F("4th Channel "));
    Serial.println(ppm[3]);
    Serial.println(F(""));
    Serial.println(F(""));
  } 

  if(error == 0){
    //What gyro is connected
    Serial.println(F(""));
    Serial.println(F("==================================================="));
    Serial.println(F("Gyro search"));
    Serial.println(F("==================================================="));
    delay(2000);
    
    Serial.println(F("Searching for MPU-6050 on address 0x68/104"));
    delay(1000);
    if(search_gyro(0x68, 0x75) == 0x68){
      Serial.println(F("MPU-6050 found on address 0x68"));
      type = 1;
      gyro_address = 0x68;
    }
    
    if(type == 0){
      Serial.println(F("Searching for MPU-6050 on address 0x69"));
      delay(1000);
      if(search_gyro(0x69, 0x75) == 0x68){
        Serial.println(F("MPU-6050 found on address 0x69"));
        type = 1;
        gyro_address = 0x69;
      }
    }
    
    
   
    
    if(type == 0){
      Serial.println(F("No gyro device found!!! (ERROR )"));
      error = 1;
    }
    
    else{
      delay(3000);
      Serial.println(F(""));
      Serial.println(F("==================================================="));
      Serial.println(F("Gyro register settings"));
      Serial.println(F("==================================================="));
      start_gyro(); //Setup the gyro for further use
    }
  }
  
  //If the gyro is found we can setup the correct gyro axes.
  if(error == 0){
    delay(3000);
    Serial.println(F(""));
    Serial.println(F("==================================================="));
    Serial.println(F("Gyro calibration"));
    Serial.println(F("==================================================="));
    Serial.println(F("Don't move the quadcopter!! Calibration starts in 3 seconds"));
    delay(3000);
    Serial.println(F("Calibrating the gyro, this will take +/- 8 seconds"));
    Serial.print(F("Please wait"));
    //Let's take multiple gyro data samples so we can determine the average gyro offset (calibration).
    for (cal_int = 0; cal_int < 2000 ; cal_int ++){              //Take 2000 readings for calibration.
      if(cal_int % 100 == 0)Serial.print(F("."));                //Print dot to indicate calibration.
      gyro_signalen();                                           //Read the gyro output.
      gyro_roll_cal += gyro_roll;                                //Ad roll value to gyro_roll_cal.
      gyro_pitch_cal += gyro_pitch;                              //Ad pitch value to gyro_pitch_cal.
      gyro_yaw_cal += gyro_yaw;                                  //Ad yaw value to gyro_yaw_cal.
      delay(4);                                                  //Wait 3 milliseconds before the next loop.
    }
    //Now that we have 2000 measures, we need to devide by 2000 to get the average gyro offset.
    gyro_roll_cal /= 2000;                                       //Divide the roll total by 2000.
    gyro_pitch_cal /= 2000;                                      //Divide the pitch total by 2000.
    gyro_yaw_cal /= 2000;                                        //Divide the yaw total by 2000.
    
    //Show the calibration results
    Serial.println(F(""));
    Serial.print(F("Axis 1 offset="));
    Serial.println(gyro_roll_cal);
    Serial.print(F("Axis 2 offset="));
    Serial.println(gyro_pitch_cal);
    Serial.print(F("Axis 3 offset="));
    Serial.println(gyro_yaw_cal);
    Serial.println(F(""));
    
    Serial.println(F("==================================================="));
    Serial.println(F("Gyro axes configuration"));
    Serial.println(F("==================================================="));
    
    //Detect the left wing up movement
    Serial.println(F("Lift the left side of the quadcopter to a 45 degree angle within 10 seconds"));
    //Check axis movement
    check_gyro_axes(1);
    if(error == 0){
      Serial.println(F("OK!"));
      Serial.print(F("Angle detection = "));
      Serial.println(roll_axis & 0b00000011);
      if(roll_axis & 0b10000000)Serial.println(F("Axis inverted = yes"));
      else Serial.println(F("Axis inverted = no"));
      Serial.println(F("Put the quadcopter back in its original position"));
      Serial.println(F("Move stick 'nose up' and back to center to continue"));
      check_to_continue();

      //Detect the nose up movement
      Serial.println(F(""));
      Serial.println(F(""));
      Serial.println(F("Lift the nose of the quadcopter to a 45 degree angle within 10 seconds"));
      //Check axis movement
      check_gyro_axes(2);
    }
    if(error == 0){
      Serial.println(F("OK!"));
      Serial.print(F("Angle detection = "));
      Serial.println(pitch_axis & 0b00000011);
      if(pitch_axis & 0b10000000)Serial.println(F("Axis inverted = yes"));
      else Serial.println(F("Axis inverted = no"));
      Serial.println(F("Put the quadcopter back in its original position"));
      Serial.println(F("Move stick 'nose up' and back to center to continue"));
      check_to_continue();
      
      //Detect the nose right movement
      Serial.println(F(""));
      Serial.println(F(""));
      Serial.println(F("Rotate the nose of the quadcopter 45 degree to the right within 10 seconds"));
      //Check axis movement
      check_gyro_axes(3);
    }
    if(error == 0){
      Serial.println(F("OK!"));
      Serial.print(F("Angle detection = "));
      Serial.println(yaw_axis & 0b00000011);
      if(yaw_axis & 0b10000000)Serial.println(F("Axis inverted = yes"));
      else Serial.println(F("Axis inverted = no"));
      Serial.println(F("Put the quadcopter back in its original position"));
      Serial.println(F("Move stick 'nose up' and back to center to continue"));
      check_to_continue();
    }
  }
  if(error == 0){
    Serial.println(F(""));
    Serial.println(F("==================================================="));
    Serial.println(F("LED test"));
    Serial.println(F("==================================================="));
    digitalWrite(12, HIGH);
    Serial.println(F("The LED should now be lit"));
    Serial.println(F("Move stick 'nose up' and back to center to continue"));
    check_to_continue();
    digitalWrite(12, LOW);
  }
  
  Serial.println(F(""));
  
  if(error == 0){
    Serial.println(F("==================================================="));
    Serial.println(F("Final setup check"));
    Serial.println(F("==================================================="));
    delay(1000);
    if(gyro_check_byte == 0b00000111){
      Serial.println(F("Gyro axes ok"));
    }
    else{
      Serial.println(F("Gyro exes verification failed!!! (ERROR 7)"));
      error = 1;
    }
  }     
  
  if(error == 0){
    //If all is good, store the information in the EEPROM
    Serial.println(F(""));
    Serial.println(F("==================================================="));
    Serial.println(F("Storing EEPROM information"));
    Serial.println(F("==================================================="));
    Serial.println(F("Writing EEPROM"));
    delay(1000);
    Serial.println(F("Done!"));
    EEPROM.write(28, roll_axis);
    EEPROM.write(29, pitch_axis);
    EEPROM.write(30, yaw_axis);
    EEPROM.write(31, type);
    EEPROM.write(32, gyro_address);
    //Write the EEPROM signature
    EEPROM.write(33, 'J'); 
    EEPROM.write(34, 'M');
    EEPROM.write(35, 'B');
        
    
    //To make sure evrything is ok, verify the EEPROM data.
    Serial.println(F("Verify EEPROM data"));
    delay(1000);
    if(roll_axis != EEPROM.read(28))error = 1;
    if(pitch_axis != EEPROM.read(29))error = 1;
    if(yaw_axis != EEPROM.read(30))error = 1;
    if(type != EEPROM.read(31))error = 1;
    if(gyro_address != EEPROM.read(32))error = 1;
    
    if('J' != EEPROM.read(33))error = 1;
    if('M' != EEPROM.read(34))error = 1;
    if('B' != EEPROM.read(35))error = 1;
  
    if(error == 1)Serial.println(F("EEPROM verification failed!!! (ERROR 5)"));
    else Serial.println(F("Verification done"));
  }
  
  
  if(error == 0){
    Serial.println(F("Setup is finished."));
    Serial.println(F("You can now calibrate the esc's and upload the Calibration code."));
  }
}



//==============================================================
  webSocket.loop();
//  ArduinoOTA.handle();
  if(captive_portal)
    dnsServer.processNextRequest();
  server.handleClient();
    // Serial.println("R %d RP %d T %d Y %d L1 %d L2 %d", (int)ppm[0], (int)ppm[1],  (int)ppm[2],  (int)ppm[3],  (int)ppm[4],  (int)ppm[5]);
     Serial.print("Row "); Serial.print(ppm[0]); 
     Serial.print("Pitch "); Serial.print(ppm[1]);
     Serial.print("Throttle "); Serial.print(ppm[2]);
     Serial.print("Yaw "); Serial.print(ppm[3]); 
     Serial.print("L1 "); Serial.print(ppm[4]);
     Serial.print("L2 "); Serial.print(ppm[5]);
     Serial.println();
     
  if(alivecount>1000){
    for(int i=0; i<4;i++){
      ppm[i]=900;
    }
    for(int i=4; i<8;i++){
      ppm[i]=1100;
    }
    
  }
 
  yield();
}


byte search_gyro(int gyro_address, int who_am_i){
  Wire.beginTransmission(gyro_address);
  Wire.write(who_am_i);
  Wire.endTransmission();
  Wire.requestFrom(gyro_address, 1);
  timer = millis() + 100;
  while(Wire.available() < 1 && timer > millis());
  lowByte = Wire.read();
  address = gyro_address;
  return lowByte;
}

void start_gyro(){
  //Setup the L3G4200D or L3GD20H
  //Setup the MPU-6050
  if(type == 1){
    
    Wire.beginTransmission(address);                             //Start communication with the gyro
    Wire.write(0x6B);                                            //PWR_MGMT_1 register
    Wire.write(0x00);                                            //Set to zero to turn on the gyro
    Wire.endTransmission();                                      //End the transmission
    
    Wire.beginTransmission(address);                             //Start communication with the gyro
    Wire.write(0x6B);                                            //Start reading @ register 28h and auto increment with every read
    Wire.endTransmission();                                      //End the transmission
    Wire.requestFrom(address, 1);                                //Request 1 bytes from the gyro
    while(Wire.available() < 1);                                 //Wait until the 1 byte is received
    Serial.print(F("Register 0x6B is set to:"));
    Serial.println(Wire.read(),BIN);
    
    Wire.beginTransmission(address);                             //Start communication with the gyro
    Wire.write(0x1B);                                            //GYRO_CONFIG register
    Wire.write(0x08);                                            //Set the register bits as 00001000 (500dps full scale)
    Wire.endTransmission();                                      //End the transmission
    
    Wire.beginTransmission(address);                             //Start communication with the gyro (adress 1101001)
    Wire.write(0x1B);                                            //Start reading @ register 28h and auto increment with every read
    Wire.endTransmission();                                      //End the transmission
    Wire.requestFrom(address, 1);                                //Request 1 bytes from the gyro
    while(Wire.available() < 1);                                 //Wait until the 1 byte is received
    Serial.print(F("Register 0x1B is set to:"));
    Serial.println(Wire.read(),BIN);

  }
}

void gyro_signalen(){
  if(type == 1){
    Wire.beginTransmission(address);                             //Start communication with the gyro
    Wire.write(0x43);                                            //Start reading @ register 43h and auto increment with every read
    Wire.endTransmission();                                      //End the transmission
    Wire.requestFrom(address,6);                                 //Request 6 bytes from the gyro
    while(Wire.available() < 6);                                 //Wait until the 6 bytes are received
    gyro_roll=Wire.read()<<8|Wire.read();                        //Read high and low part of the angular data
    if(cal_int == 2000)gyro_roll -= gyro_roll_cal;               //Only compensate after the calibration
    gyro_pitch=Wire.read()<<8|Wire.read();                       //Read high and low part of the angular data
    if(cal_int == 2000)gyro_pitch -= gyro_pitch_cal;             //Only compensate after the calibration
    gyro_yaw=Wire.read()<<8|Wire.read();                         //Read high and low part of the angular data
    if(cal_int == 2000)gyro_yaw -= gyro_yaw_cal;                 //Only compensate after the calibration
  }
}