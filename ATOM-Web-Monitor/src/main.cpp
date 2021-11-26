/******************************************************************************
 * M5ATOM ENV web-monitor
 * A simple web server to display the environment sensor data as a web page.
 * 
 * Hague Nusseck @ electricidea
 * v1.0 | 26.November.2021
 * https://github.com/electricidea/M5ATOM/tree/master/ATOM-Web-Monitor
 * 
 * 
 * used Resources:
 * https://github.com/espressif/arduino-esp32/blob/master/libraries/WiFi/examples/SimpleWiFiServer/SimpleWiFiServer.ino
 * https://realfavicongenerator.net/
 * https://github.com/Jamesits/bin2array
 * https://www.deadnode.org/sw/bin2h/
 * https://html-css-js.com/css/generator/font/
 * 
 * Distributed as-is; no warranty is given.
 ******************************************************************************/
#include <Arduino.h>


#include "M5Atom.h"
// install the M5ATOM library:
// pio lib install "M5Atom"

// You also need the FastLED library
// https://platformio.org/lib/show/126/FastLED
// FastLED is a library for programming addressable rgb led strips 
// (APA102/Dotstar, WS2812/Neopixel, LPD8806, and a dozen others) 
// acting both as a driver and as a library for color management and fast math.
// install the library:
// pio lib install "fastled/FastLED"

#define LED_ERROR   0x110000
#define LED_OK      0x001100
#define LED_NETWORK 0x000011
#define LED_MEASURE 0x111111

unsigned long next_millis;


#include "UNIT_ENV.h"
// ENVIII:
// SHT30:   temperature and humidity sensor  I2C: 0x44
// QMP6988: absolute air pressure sensor     I2C: 0x70
SHT3X sht30;
QMP6988 qmp6988;

float qmp_Pressure = 0.0;
float sht30_Temperature = 0.0;
float sht30_Humidity = 0.0;
int n_average = 1;

// WIFI and https client librarys:
#include "WiFi.h"
#include <WiFiClientSecure.h>

// WiFi network configuration:
char wifi_ssid[33];
char wifi_key[65];
const char* ssid     = "YourWiFi";
const char* password = "YourPassword";

WiFiClient myclient;
WiFiServer server(80);

// GET 
#define GET_unknown 0
#define GET_index_page  1
#define GET_favicon  2
#define GET_logo  3
#define GET_script  4
int html_get_request;

#include "index.h"
#include "electric_logo.h"
#include "favicon.h"

// forward declarations:
void I2Cscan();
boolean connect_Wifi();

void setup() {
  // start the ATOM device with Serial and Display (one LED)
  // begin(SerialEnable, I2CEnable, DisplayEnable)
  M5.begin(true, false, true);
  // Wire.begin() must be called after M5.begin()
  // Atom Matrix I2C GPIO Pin is 26 and 32
  Wire.begin(26, 32);         
  delay(50); 
  // set LED to red
  M5.dis.fillpix(LED_ERROR); 
  Serial.println("M5ATOM ENV montitor");
  Serial.println("v1.0 | 26.11.2021");
  // scan for I2C devices
  I2Cscan();
  // Set WiFi to station mode and disconnect
  // from an AP if it was previously connected
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(1000);
  // connect to the configured AP
  connect_Wifi();
  // Start TCP/IP-Server
  server.begin();     
  

  if(qmp6988.init()==1){
    Serial.println("[OK] QMP6988 ready");
  } else {
    Serial.println("[ERR] QMP6988 not ready");
  }
  // high precission mode
  qmp6988.setFilter(QMP6988_FILTERCOEFF_32);
  qmp6988.setOversamplingP(QMP6988_OVERSAMPLING_32X);
  qmp6988.setOversamplingT(QMP6988_OVERSAMPLING_4X);
  next_millis = millis() + 1000;
}

void loop() {
  // get actual time in miliseconds
  unsigned long current_millis = millis();
  // check if next measure interval is reached
  if(current_millis > next_millis){
    Serial.println("Measure");
    next_millis = current_millis + 3000;
    M5.dis.fillpix(LED_MEASURE); 
    if (sht30.get() != 0) {
      return;
    }
    Serial.println(qmp6988.calcPressure());
    Serial.println(sht30.cTemp);
    Serial.println(sht30.humidity);
    // calculate running average
    qmp_Pressure = ((qmp_Pressure*(n_average-1)) + qmp6988.calcPressure())/n_average;
    sht30_Temperature = ((sht30_Temperature*(n_average-1)) + sht30.cTemp)/n_average;
    sht30_Humidity = ((sht30_Humidity*(n_average-1)) + sht30.humidity)/n_average;
    if(n_average < 10) 
      n_average++;
  }
  // check if WIFI is still connected
  // if the WIFI is not connected (anymore)
  // a reconnect is triggert
  wl_status_t wifi_Status = WiFi.status();
  if(wifi_Status != WL_CONNECTED){
    // set LED to red
    M5.dis.fillpix(LED_ERROR); 
    // reconnect if the connection get lost
    Serial.println("[ERR] Lost WiFi connection, reconnecting...");
    if(connect_Wifi()){
      Serial.println("[OK] WiFi reconnected");
    } else {
      Serial.println("[ERR] unable to reconnect");
    }
  }
  // check if WIFI is connected
  // needed because of the above mentioned reconnection attempt
  wifi_Status = WiFi.status();
  if(wifi_Status == WL_CONNECTED){
    // set LED to green
    M5.dis.fillpix(LED_OK); 
  }

  // check for incoming clients
  WiFiClient client = server.available(); 
  if (client) {  
    // force a disconnect after 1 second
    unsigned long timeout_millis = millis()+1000;
    // set LED to blue
    M5.dis.fillpix(LED_NETWORK); 
    Serial.println("New Client.");  
    // a String to hold incoming data from the client line by line        
    String currentLine = "";                
    // loop while the client's connected
    while (client.connected()) { 
      // if the client is still connected after 1 second,
      // something is wrong. So kill the connection
      if(millis() > timeout_millis){
        Serial.println("Force Client stop!");  
        client.stop();
      } 
      // if there's bytes to read from the client,
      if (client.available()) {             
        char c = client.read();            
        Serial.write(c);    
        // if the byte is a newline character             
        if (c == '\n') {    
          // two newline characters in a row (empty line) are indicating
          // the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line,
            // followed by the content:
            switch (html_get_request)
            {
              case GET_index_page: {
                client.println("HTTP/1.1 200 OK");
                client.println("Content-type:text/html");
                client.println();
                client.write_P(index_html, sizeof(index_html));
                break;
              }

              case GET_favicon: {
                client.println("HTTP/1.1 200 OK");
                client.println("Content-type:image/x-icon");
                client.println();
                client.write_P(electric_favicon, sizeof(electric_favicon));
                break;
              }

              case GET_logo: {
                client.println("HTTP/1.1 200 OK");
                client.println("Content-type:image/jpeg");
                client.println();
                client.write_P(electric_logo, sizeof(electric_logo));
                break;
              }

              case GET_script: {              
                client.println("HTTP/1.1 200 OK");
                client.println("Content-type:application/javascript");
                client.println();
                client.printf("var temperatureValue = %3.2f;\n", sht30_Temperature);
                client.printf("var humidityValue = %3.2f;", sht30_Humidity);
                client.printf("var pressureValue = %3.2f;", qmp_Pressure/100.0F);
                break;
              }
              
              default:
                client.println("HTTP/1.1 404 Not Found");
                client.println("Content-type:text/html");
                client.println();
                client.print("404 Page not found.<br>");
                break;
            }
            // The HTTP response ends with another blank line:
            client.println();
            // break out of the while loop:
            break;
          } else {    // if a newline is found
            // Analyze the currentLine:
            // detect the specific GET requests:
            if(currentLine.startsWith("GET /")){
              html_get_request = GET_unknown;
              // if no specific target is requested
              if(currentLine.startsWith("GET / ")){
                html_get_request = GET_index_page;
              }
              // if the logo image is requested
              if(currentLine.startsWith("GET /electric-idea_100x100.jpg")){
                html_get_request = GET_logo;
              }
              // if the favicon icon is requested
              if(currentLine.startsWith("GET /favicon.ico")){
                html_get_request = GET_favicon;
              }
              // if the dynamic script part is requested
              if(currentLine.startsWith("GET /data.js")){
                html_get_request = GET_script;
              }
            }
            currentLine = "";
          }
        } else if (c != '\r') {  
          // add anything else than a carriage return
          // character to the currentLine 
          currentLine += c;      
        }
      }
    }
    // close the connection:
    client.stop();
    Serial.println("Client Disconnected.");
  }
}

//==============================================================
void I2Cscan(){
  // scan for i2c devices
  byte error, address;
  int nDevices;

  Serial.println("Scanning I2C bus...\n");

  nDevices = 0;
  for(address = 1; address < 127; address++ ){
    // The i2c_scanner uses the return value of
    // the Write.endTransmisstion to see if
    // a device did acknowledge to the address.
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    // Errors:
    //  0 : Success
    //  1 : Data too long
    //  2 : NACK on transmit of address
    //  3 : NACK on transmit of data
    //  4 : Other error
    if (error == 0){
      nDevices++;
      Serial.printf("[OK] %i 0x%.2X\n", nDevices, address);
    } else{
      if(error == 4)
        Serial.printf("[ERR] %i 0x%.2X\n", nDevices, address);
    }
  }
  Serial.printf("\n%i devices found\n\n", nDevices);
 }

// =============================================================
// connect_Wifi()
// connect to configured Wifi Access point
// returns true if the connection was successful otherwise false
// =============================================================
boolean connect_Wifi(){
  // Establish connection to the specified network until success.
  // Important to disconnect in case that there is a valid connection
  WiFi.disconnect();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  delay(1500);
  //Start connecting (done by the ESP in the background)
  WiFi.begin(ssid, password);
  // read wifi Status
  wl_status_t wifi_Status = WiFi.status();
  int n_trials = 0;
  // loop while Wifi is not connected
  // run only for 20 trials.
  while (wifi_Status != WL_CONNECTED && n_trials < 20) {
    // Check periodicaly the connection status using WiFi.status()
    // Keep checking until ESP has successfuly connected
    wifi_Status = WiFi.status();
    n_trials++;
    switch(wifi_Status){
      case WL_NO_SSID_AVAIL:
          Serial.println("[ERR] WIFI SSID not available");
          break;
      case WL_CONNECT_FAILED:
          Serial.println("[ERR] WIFI Connection failed");
          break;
      case WL_CONNECTION_LOST:
          Serial.println("[ERR] WIFI Connection lost");
          break;
      case WL_DISCONNECTED:
          Serial.println("[STATE] WiFi disconnected");
          break;
      case WL_IDLE_STATUS:
          Serial.println("[STATE] WiFi idle status");
          break;
      case WL_SCAN_COMPLETED:
          Serial.println("[OK] WiFi scan completed");
          break;
      case WL_CONNECTED:
          Serial.println("[OK] WiFi connected");
          break;
      default:
          Serial.println("[ERR] WIFI unknown Status");
          break;
    }
    delay(500);
  }
  if(wifi_Status == WL_CONNECTED){
    // if connected
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    // if not connected
    Serial.println("[ERR] unable to connect Wifi");
    return false;
  }
}
