/*
    Go to thingspeak.com and create an account if you don't have one already.
    After logging in, click on the "New Channel" button to create a new channel for your data. This is where your data will be stored and displayed.
    Fill in the Name, Description, and other fields for your channel as desired, then click the "Save Channel" button.
    Take note of the "Write API Key" located in the "API keys" tab, this is the key you will use to send data to your channel.
    Replace the channelID from tab "Channel Settings" and privateKey with "Read API Keys" from "API Keys" tab.
    Replace the host variable with the thingspeak server hostname "api.thingspeak.com"
    Upload the sketch to your ESP32 board and make sure that the board is connected to the internet. The ESP32 should now send data to your Thingspeak channel at the intervals specified by the loop function.
    Go to the channel view page on thingspeak and check the "Field1" for the new incoming data.
    You can use the data visualization and analysis tools provided by Thingspeak to display and process your data in various ways.
    Please note, that Thingspeak accepts only integer values.
 */

//Config
#include <WiFi.h>

const char *ssid = "KTO-Rosomak";
const char *password = "12345678";

const char *host = "api.thingspeak.com";
const int httpPort = 80;
const String channelID = "3031573";
const String writeApiKey = "OPIFZT6VIE3R7S4Q";
const String readApiKey = "NN7UC55NWICGAPB7";

// The default example accepts one data filed named "field1"
// For your own server you can ofcourse create more of them.
int field1 = 0;
int field2 = 0;
int field3 = 0;
int field4 = 0;

int numberOfResults = 0;  // Number of results to be read
int fieldNumber = 1;      // Field number which will be read out
///////////////////////////////////////////////////////////////////////////////////

// Setup
void setup() {
  Serial.begin(115200);

  // We start by connecting to a WiFi network

  Serial.println();
  Serial.println("******************************************************");
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}
///////////////////////////////////////////////////////////////////////////////////

// Reading from server
void readResponse(NetworkClient *client) {
  unsigned long timeout = millis();
  while (client->available() == 0) {
    if (millis() - timeout > 5000) {
      Serial.println(">>> Client Timeout !");
      client->stop();
      return;
    }
  }

  // Read all the lines of the reply from server and print them to Serial
  while (client->available()) {
    String line = client->readStringUntil('\r');
    Serial.print(line);
  }

  Serial.printf("\nClosing connection\n\n");
}
///////////////////////////////////////////////////////////////////////////////////

// Looping code
void loop() {
  NetworkClient client;
  
  // WRITE -------------------------------------------------------------------------------------------
  String footer = String(" HTTP/1.1\r\n") + "Host: " + String(host) + "\r\n" + "Connection: close\r\n\r\n";
  String getRequest = "/update?api_key=" + writeApiKey +
                      "&field1=" + String(field1) +
                      "&field2=" + String(field2) +
                      "&field3=" + String(field3) +
                      "&field4=" + String(field4);

  if (client.connect(host, httpPort)) {
    client.print("GET " + getRequest + footer);
    readResponse(&client);
  }

  ++field1;
  ++field2;
  ++field3;
  ++field4;

  delay(16000);
}
