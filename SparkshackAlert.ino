/*

*/

#include <EEPROM.h>

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <WiFiClient.h>

#include <Time.h>         //http://www.arduino.cc/playground/Code/Time
#include <Timezone.h>     //https://github.com/LelandSindt/Timezone

#include <PubSubClient.h> //https://github.com/knolleary/pubsubclient/releases/tag/2.4

#include <SPI.h>
#include <Wire.h>


bool spacestate;
bool klok_ok = false;
uint16_t gluurders[11] = {0};

// WiFi settings (comment include and uncomment ssid/pass lines below)
#include <wifikey.h>

//char ssid[] = "";  //  your network SSID (name)
//char pass[] = "";       // your network password

// Timezone Rules for Europe
// European Daylight time begins on the last sunday of March
// European Standard time begins on the last sunday of October
// I hope i got the correct hour set for the time-change rule (5th parameter in the rule).

TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, +120};    //Daylight time = UTC +2 hours
TimeChangeRule CET = {"CET", Last, Sun, Oct, 3, +60};     //Standard time = UTC +1 hours
Timezone myTZ(CEST, CET);
TimeChangeRule *tcr;        //pointer to the time change rule, use to get TZ abbrev
time_t utc, local;

// NTP Server settings and preparations
unsigned int localPort = 2390;      // local port to listen for UDP packets
IPAddress timeServerIP; // time.nist.gov NTP server address
const char* ntpServerName = "pool.ntp.org";
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
WiFiUDP udp;

// MQTT Server settings and preparations
const char* mqtt_server = "test.mosquitto.org";
WiFiClient espClient;
PubSubClient client(mqtt_server, 1883, onMqttMessage, espClient);
long lastReconnectAttempt = 0;

// Squeeze information
const char http_site[] = "jukebox.space.revspace.nl";
const int http_port = 80;
const char player[] = "d8%3Ad3%3A85%3A17%3A30%3A9c"; // bar sparkshack
// const char player[] = "be%3Ae0%3Ae6%3A04%3A46%3A38"; // klusbunker

// strings for jukebox
const char playlist[] = "playlist";
const char mixer[] = "mixer";
const char jumpcommand[] = "jump";
const char stopcommand[] = "stop";
const char emptypar[] = "%2B1";
const char volume[] = "volume";
const char highvolume[] = "%2B5";
const char lowvolume[] = "-5";
const char insertcommand[] = "insert";
const char eetmuziek[] = "file%3A%2F%2F%2Fmusic%2FLosse%20Tracks%2Ftroll.mp3";

void setup()
{
  Serial.begin(115200);
  Serial.println();


  // We start by connecting to a WiFi network
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);
  delay(1);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(50);
  }
  Serial.println();
  Serial.println("WiFi connected");
  Serial.println("Connected!");
  Serial.println(WiFi.SSID());
  Serial.println(WiFi.localIP());
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");

  udp.begin(localPort);
  ntpsync();
}

void loop()
{
  if (!client.connected()) {
    Serial.println(".");
    long verstreken = millis();
    if (verstreken - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = verstreken;
      // Attempt to reconnect
      if (reconnect()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    // Client connected
    client.loop();
  }

  if (spacestate == HIGH) {
    if (millis() % 1000 == 0) {
      //printTime(now());
    }
  } else {

  }

  // NTP sync every 3 hours.
  if (hour(now()) % 3 == 0 && minute(now()) == 0 && second(now()) == 0) {
    klok_ok = false;
  }

  if (!klok_ok) {
    ntpsync();
  }
}


boolean reconnect() {
  if (client.connect("SparkshackAlert")) {
    // Once connected, publish an announcement...
    client.publish("revspace/ssalert", "I'm here");
    // ... and resubscribe
    client.subscribe("revspace/state");
    client.loop();
    client.subscribe("revspace/button/nomz");
    client.loop();
    client.subscribe("revspace/button/doorbell");
    client.loop();
    client.subscribe("revspace/cams");
    client.loop();
  }
  return client.connected();
}



boolean ntpsync() {
  //get a random server from the pool
  WiFi.hostByName(ntpServerName, timeServerIP);
  sendNTPpacket(timeServerIP); // send an NTP packet to a time server
  delay(500);
  int cb = udp.parsePacket();
  if (!cb) {
    Serial.println("no packet yet");
    klok_ok = false;
  }
  else {
    Serial.print("packet received, length=");
    Serial.println(cb);
    // We've received a packet, read the data from it
    udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    Serial.print("Seconds since Jan 1 1900 = " );
    Serial.println(secsSince1900);

    // now convert NTP time into everyday time:
    Serial.print("Unix time = ");
    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    unsigned long epoch = secsSince1900 - seventyYears;
    // print Unix time:
    Serial.println(epoch);

    local = myTZ.toLocal(epoch, &tcr);

    setTime(local);
    klok_ok = true;
  }
}



// send an NTP request to the time server at the given address
unsigned long sendNTPpacket(IPAddress& address)
{
  Serial.println("sending NTP packet...");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}


void printTime(time_t t)
{
  if (hour(t) < 10) {
    Serial.print("0");
  }
  Serial.print(hour(t));
  Serial.print(":");

  if (minute(t) < 10) {
    Serial.print("0");
  }
  Serial.print(minute(t));
  Serial.print(":");
  if (second(t) < 10) {
    Serial.print("0");
  }
  Serial.println(second(t));
}

void printDate(time_t t)
{
  Serial.print(day(t));
  Serial.print("-");
  Serial.print(month(t));
  Serial.print("-");
  Serial.print(year(t));
}


void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  uint16_t spaceCnt;
  uint8_t numCnt = 0;
  char bericht[50] = "";

  Serial.print("received topic: ");
  Serial.println(topic);
  Serial.print("length: ");
  Serial.println(length);
  Serial.print("payload: ");
  for (uint8_t pos = 0; pos < length; pos++) {
    bericht[pos] = payload[pos];
  }
  Serial.println(bericht);
  Serial.println();

  // Lets select a topic/payload handler
  // Some topics (buttons for example) don't need a specific payload handled, just a reaction to the topic. Saves a lot of time!


  // Space State
  if (strcmp(topic, "revspace/state") == 0) {


    if (strcmp(bericht, "open") == 0) {
      Serial.println("Revspace is open");
      if (spacestate == LOW) {
        spacestate = HIGH;
      }
    } else {
      // If it ain't open, it's closed! (saves a lot of time doing strcmp).
      Serial.println("Revspace is dicht");
      if (spacestate == HIGH) {
        spacestate = LOW;
      }
    }
  }

  // NOMZ because we are hungry! Lets join the blinking lights parade!
  if (strcmp(topic, "revspace/button/nomz") == 0) {
    // Play the music
    getPage(playlist, insertcommand, eetmuziek);
    getPage(playlist, jumpcommand , emptypar);
  }

  // DOORBELL
  if (strcmp(topic, "revspace/button/doorbell") == 0) {

  }

  if (strcmp(topic, "revspace/cams") == 0) {
    // This part was written by Benadski@Revspace, many thanks!
    // Modified it so the function fills an array with all available cam-stats
    char num[3] = "";
    for (uint8_t teller = 0; teller < 10; teller++) {
      spaceCnt = 0;
      numCnt = 0;
      memset(num, 0, sizeof(num));

      for (uint8_t skip = 0; skip < teller; skip++) {
        while (((uint8_t)payload[spaceCnt] != 32) && (spaceCnt < length)) {
          spaceCnt++;
        }
        if (((uint8_t)payload[spaceCnt] == 32) && (spaceCnt < length)) spaceCnt++;
      }

      while (((uint8_t)payload[spaceCnt] != 32) && (spaceCnt <= length) && (numCnt < 3)) {
        num[numCnt] = payload[spaceCnt];
        numCnt++;
        spaceCnt++;
      }
      if (numCnt > 0) {
        gluurders[teller] = atoi(&num[0]);
      }
    }

    if (gluurders[0] > 0) {

      Serial.println("Aantal gluurders");
      for (uint8_t teller = 0; teller < 10; teller++) {
        if (gluurders[teller] > 0) {
          if (teller == 0) {
            Serial.print("Totaal");
          } else {
            Serial.print("Cam ");
            Serial.print(teller);
          }
          Serial.print(": ");
          Serial.println(gluurders[teller]);
        }
      }
    } else {
      Serial.println("geen gluurders :(");
    }
  }

  Serial.println();
}


// Perform an HTTP GET request to a remote page
bool getPage(const char *p0, const char *p1, const char *p2) {
  if ( !espClient.connect(http_site, http_port) ) {
    Serial.println("Message fail.");
    return false;
  }

  // Make an HTTP GET request
  espClient.print("GET /Classic/status_header.html?p0=");
  espClient.print(p0);
  espClient.print("&p1=");
  espClient.print(p1);
  espClient.print("&p2=");
  espClient.print(p2);
  espClient.print("&player=");
  espClient.print(player);
  espClient.println (" HTTP/1.1");
  espClient.print("Host: ");
  espClient.println(http_site);
  espClient.println("Connection: close");
  espClient.println();
  delay(50);
  Serial.println("Message sent.");

  return true;
}

