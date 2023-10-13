// UpGoV Ground Station Application
// Version: Beta 1.0
// Author: Bob Parker
// Date: 10/13/2023

// This is an initial version of the UpGoV ground station application that does
// not interface with an iPhone app (to be added later). This ground station app
// uses two 128 x 64 dot OLED displays to show UpGoV messages and data.
//
// The user must press button A to dismiss the opening splash screen and start
// interacting with the UpGoV instrumentation via the RFM95 LoRa radio.
// Current functionality includes:
// 1. Press the A button to send an "arm" command to the UpGoV
// 2. Press the A button to send a "disarm" command to the UpGoV

// Load into the "red" LoRa Radio Feather module
#include<Wire.h>            // I2C Library for OLED display comms
#include<SPI.h>              // SPI library for LoRa Radio comms
#include<Adafruit_GFX.h>    // Graphics library for OLED display
#include<Adafruit_SH110X.h> // OLED display driver library
#include<RH_RF95.h>         // LoRa Radio driver library

// Constants
const uint8_t OLED_A_ADDR = 0x3C;
const uint8_t OLED_B_ADDR = 0x3D;
const uint8_t BUTTON_A = 9;
const uint8_t BUTTON_B = 6;
const uint8_t BUTTON_C = 5;
const uint8_t RADIO_CS = 8;
const uint8_t RADIO_INT = 3;
const uint8_t RADIO_RST = 4;
const double RADIO_FREQ = 915.0;
const String VERSION = "Beta 1.0";
const uint8_t RADIO_POWER = 23;
const uint8_t MAX_MESSAGE_LENGTH = 20;

enum oledDataType {STATUS, ALTITUDE, ACCELERATION, DURATION, BATTERY1, BATTERY2};
enum states {START_UP, FAULT, READY, ARMED, LOGGING, POST_FLIGHT, ERROR};
enum battery {FULL, OK, LOWBAT, CRITICAL};

// Global Variables and Objects
Adafruit_SH1107 displayA = Adafruit_SH1107(64, 128, &Wire);
Adafruit_SH1107 displayB = Adafruit_SH1107(64, 128, &Wire);
RH_RF95 radioDriver = RH_RF95(RADIO_CS, RADIO_INT);

bool armed = false;

char buffer[MAX_MESSAGE_LENGTH]; // Message buffer
char radioPacket[20];


void setup() {
  Serial.begin(115200);
  while(!Serial) {
    ;
  }

  Serial.print("UpGoV Ground Station Version ");
  Serial.println(VERSION);

  pinMode(BUTTON_A, INPUT_PULLUP);
  pinMode(RADIO_RST, OUTPUT);
  digitalWrite(RADIO_RST, HIGH);

  delay(1000);  // Wait for the OLED displays to power up
  displayA.begin(OLED_A_ADDR, true);
  displayB.begin(OLED_B_ADDR, true);

  // Display the OLED splash screen
  displayA.display();
  displayB.display();

  // Configure the radio
  digitalWrite(RADIO_RST, LOW);
  delay(10);
  digitalWrite(RADIO_RST, HIGH);
  delay(10);

  bool radioFreqSetError;
  bool radioInitOK = radioDriver.init();
  if (radioInitOK) {
    radioFreqSetError = !radioDriver.setFrequency(RADIO_FREQ);
    radioDriver.setTxPower(RADIO_POWER, false);
  }
  

  // Wait for user to press button A
  while(digitalRead(BUTTON_A)) {
    ;
  }

  displayA.clearDisplay();
  displayA.display();
  displayB.clearDisplay();
  displayB.display();

  // Configure OLED displays
  displayA.setRotation(1);
  displayB.setRotation(1);
  displayA.setTextSize(1);
  displayA.setTextColor(SH110X_WHITE, SH110X_BLACK);
  displayB.setTextSize(1);
  displayB.setTextColor(SH110X_WHITE, SH110X_BLACK);
  displayB.setCursor(0,0);

  // Initialize the data OLED screen
  displayB.print("Status: ");
  displayB.setCursor(0, 10);
  displayB.print("Altitude: ");
  displayB.setCursor(0, 20);
  displayB.print("Acceleration: ");
  displayB.setCursor(0, 30);
  displayB.print("Duration: ");
  displayB.setCursor(0, 50);
  displayB.print("Bat 1: ");
  displayB.setCursor(65, 50);
  displayB.print("Bat 2: ");
  displayB.display();

  // Show radio initialization results
  if (!radioInitOK) {
    Serial.println("Radio initialization error");
    printOledMessage("Radio init error");
  } else if (radioFreqSetError) {
    Serial.println("Error setting radio frequency");
    printOledMessage((char *)"Error setting freq");
  } else {
    Serial.println("Radio initialized");
    printOledMessage("Radio initialized");
  }
  delay(3000);
  
  // Wait here for the radio to connect to the UpGoV
  // avionics board. Send out a periodic (every 2 seconds) 
  // radio message and wait until a "connect" reply message 
  // is received.
  printOledMessage("Waiting radio connect");

  bool connected = false;
  strncpy(radioPacket, "connect", sizeof(radioPacket));
  while (!connected) {
    Serial.print("Sending: ");
    Serial.println(radioPacket);
    radioDriver.send((uint8_t *)radioPacket, sizeof(radioPacket));
    delay(10);

    // Block here until the packet has been sent by the transmitter
    radioDriver.waitPacketSent();
    Serial.println("Radio packet sent");

    // Now check for receipt of a "connect" message
    if (radioDriver.waitAvailableTimeout(1000)) {    // Check for a message for 1 second
      // There should be a message for us
      Serial.println("Message available");
      uint8_t length;
      if (radioDriver.recv((uint8_t *)buffer, &length)) {   // Get the message
        // Check the length
        if (length != 0) {
          Serial.println("Message length OK");
          if (!strncmp(buffer, "connect", 7)) {   // Is it the "connect" message
            // It is!!
            Serial.println("Received connect message");
            connected = true;
            printOledMessage("Connected");
          }
        }
      }
    }
    
    delay(1000);
  }


  //delay(3000);
  
}

void loop() {
  // In the loop we need to periodically do the following:
  //   1. Check for new messages received via the radio
  //   2. Update displays according to the messages received
  //   3. Check for Button A press
  //      If state = ready
  //      Send the "arm" radio message if button A is pressed
  //      Confirm receipt of an "armed" message from UpGoV and
  //      update state to armed.
  //      If state = armed
  //      Send the "disarm" radio message if button A is pressed
  //      Confirm receipt of a "disarm" message from UpGoV and
  //      update state to ready
  //   4. Check for "again" message from UpGoV.
  //      If message is received then wait for user button press
  //      If button B pressed send "yes" message to UpGoV
  //      If button C pressed send "no" message to UpGoV

  static states currentState = START_UP;
  static battery currentBatteryStatus = CRITICAL;
  
  // Check for available radio message from UpGoV
  if (radioDriver.available()) {
    // Process, decode, and act on the message here
    char messageBuffer[RH_RF95_MAX_MESSAGE_LEN];
    uint8_t messageLength = sizeof(messageBuffer);
    if (radioDriver.recv((uint8_t *)messageBuffer, &messageLength)) {
      // Check message length. All valid messages will have length > 3 
      if (messageLength >= 3) {
        String message(messageBuffer);
        Serial.println(message);
        if (message.startsWith("MS:")) {
          message.remove(0, 3); // Remove the message prefix
          printOledMessage(message.c_str());
        } else if (message.startsWith("ER:")) {
          message.remove(0, 3);
          String errorMessage("ERROR: ");
          errorMessage.concat(message);
          printOledMessage(errorMessage.c_str());
        } else if (message.startsWith("ST:")) {
          message.remove(0, 3);
          printOledData(STATUS, message.c_str());
          if (message == "FAULT") {
            currentState = FAULT;
            sendRadioMessageNoAck("ST:FAULT");
          } else if (message == "READY") {
            currentState = READY;
            sendRadioMessageNoAck("ST:READY");
          } else if (message == "ARMED") {
            currentState = ARMED;
            sendRadioMessageNoAck("ST:ARMED");
          } else if (message == "LOGGING") {
            currentState = LOGGING;
          } else if (message == "POST_FLIGHT") {
            currentState = POST_FLIGHT;
            sendRadioMessageNoAck("ST:POST_FLIGHT");
          } else {
            currentState = ERROR;
          }
        } else if (message.startsWith("AL:")) {
          message.remove(0, 3);
          printOledData(ALTITUDE, message.c_str());
        } else if (message.startsWith("AC:")) {
          message.remove(0, 3);
          printOledData(ACCELERATION, message.c_str());
        } else if (message.startsWith("DU:")) {
          message.remove(0, 3);
          printOledData(DURATION, message.c_str());
        } else if (message.startsWith("AT:")) {
          printOledMessage("Lauch again?");
        } else if (message.startsWith("BT:")) {
          message.remove(0, 3);
          printOledData(BATTERY1, message.c_str());
          if (message == "FULL") {
            currentBatteryStatus = FULL;
          } else if (message == "OK") {
            currentBatteryStatus = OK;
          } else if (message == "LOW") {
            currentBatteryStatus = LOWBAT;
          } else if (message == "CRITICAL") {
            currentBatteryStatus = CRITICAL;
          } else {
            currentBatteryStatus = CRITICAL;
          }
        } else {
          printOledMessage("Invalid message");
        }
      }
    }
  } // End of incoming message processing

  // Check for button A press
  if (!digitalRead(BUTTON_A)) {
    if (currentState == READY) {
      sendRadioMessageNoAck("arm");
    } else if (currentState == ARMED) {
      sendRadioMessageNoAck("disarm");
    }
  }
  
  //delay(10);  // Repeat the loop every 10ms
}

// ================== printOledMessage =====================
// Prints a message to the OLED screen reserved for messages
// received from the UpGoV avionics.
// The OLED screen can display up to 6 individual messages
// at a time. The messages are displayed with the newest
// message showing at the bottom of the list. When the
// seventh message is received the previous messages are
// scrolled up to make room for the new message. The oldest
// message is lost at the top. There is currently no
// provision to support user scrolling to view older
// messages.
// Parameters:
//  message - The C string message that is to be displayed
// Return - None
//==========================================================
void printOledMessage(const char * message) {
  static uint8_t lineNumber = 0;
  static String line0Message = "";
  static String line1Message = "";
  static String line2Message = "";
  static String line3Message = "";
  static String line4Message = "";
  static String line5Message = "";

  if (lineNumber == 6) {
    // Scroll the messages up
    line0Message = line1Message;
    line1Message = line2Message;
    line2Message = line3Message;
    line3Message = line4Message;
    line4Message = line5Message;
    line5Message = message;

    // Clear the display and write the new messages
    displayA.clearDisplay();
    displayA.display();
    displayA.setCursor(0, 0);
    displayA.print(line0Message);
    displayA.setCursor(0, 10);
    displayA.print(line1Message);
    displayA.setCursor(0, 20);
    displayA.print(line2Message);
    displayA.setCursor(0, 30);
    displayA.print(line3Message);
    displayA.setCursor(0, 40);
    displayA.print(line4Message);
    displayA.setCursor(0, 50);
    displayA.print(line5Message);
    displayA.display();
  }
  else if (lineNumber == 5) {
    line5Message = message;
    displayA.setCursor(0, 50);
    displayA.print(line5Message);
    displayA.display();
    lineNumber++;
  } else if (lineNumber == 4) {
    line4Message = message;
    displayA.setCursor(0, 40);
    displayA.print(line4Message);
    displayA.display();
    lineNumber++;
  } else if (lineNumber == 3) {
    line3Message = message;
    displayA.setCursor(0, 30);
    displayA.print(line3Message);
    displayA.display();
    lineNumber++;
  } else if (lineNumber == 2) {
    line2Message = message;
    displayA.setCursor(0, 20);
    displayA.print(line2Message);
    displayA.display();
    lineNumber++;
  } else if (lineNumber == 1) {
    line1Message = message;
    displayA.setCursor(0, 10);
    displayA.print(line1Message);
    displayA.display();
    lineNumber++;
  } else {
    line0Message = message;
    displayA.setCursor(0, 0);
    displayA.print(line0Message);
    displayA.display();
    lineNumber++;
  }
}

void printOledData(oledDataType type, const char* data) {
  switch (type) {
    case STATUS:
      displayB.setCursor(42, 0);
      displayB.print(data);
      displayB.display();
      break;
    case ALTITUDE:
      displayB.setCursor(52, 10);
      displayB.print(data);
      displayB.display();
      break;
    case ACCELERATION:
      displayB.setCursor(77, 20);
      displayB.print(data);
      displayB.display();
      break;
    case DURATION:
      displayB.setCursor(54, 30);
      displayB.print(data);
      displayB.display();
      break;
    case BATTERY1:
      displayB.setCursor(36, 50);
      displayB.print(data);
      displayB.display();
      break;
    case BATTERY2:
      displayB.setCursor(100, 50);
      displayB.print(data);
      displayB.display();
      break;
    default:
      Serial.println("Error in printOledData()");
      break;
  }
}


//=================== sendRadioMessageNoAck ======================
// Sends a radio message with no acknowledment required.
// Parameters:
//  message:  The radio message that is to be sent
// Return: None
//=================================================================
void sendRadioMessageNoAck(const char * message) {
  strncpy(radioPacket, message, sizeof(radioPacket));
  radioDriver.send((uint8_t *)radioPacket, sizeof(radioPacket));
  delay(10);

   // Block here until the packet has been sent
   radioDriver.waitPacketSent();
}
