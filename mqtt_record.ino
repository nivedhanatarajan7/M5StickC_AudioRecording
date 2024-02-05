#include <M5StickC.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "time.h"
#include "esp_wpa2.h" //wpa2 library for connections to Enterprise networks
#include <driver/i2s.h>

//
// ZAID: put your login information here
//
char* EAP_IDENTITY  = "username";
char* EAP_PASSWORD  = "password";


char* STAusername;
char* STApassword;

uint32_t timer;


//String vars
String macAddress = WiFi.macAddress();  //"00:00:00:00:00:00";


TaskHandle_t Task1;

//time vars
double Epoch_Time;
double Epoch_millis;
double startMil;
double startTime;
double outTime;
float mqttMil;

String officalStart;
String officalEnd;

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -18000;
const int daylightOffset_sec = 0;

const int numDataPoints = 100;
size_t bytes_read;

uint16_t i2s_data[numDataPoints];
float dataArray[numDataPoints];

struct RowData {
  float audioArray[numDataPoints];
};
 
RowData rowData;

String unitArray[] = {
  "accX",
  "accY",
  "accZ",
  "gyroX",
  "gyroY",
  "gyroZ",
  "roll",
  "kalmanRoll",
  "weightedRoll",
  "posture",
  "audio",
};

int numOfUnits;
int count;
bool finishCollection = false;

//
// ZAID: This is the sample periord, change this if you want to change the frequency of the system
//
int samplePeriord = 10; // 10 ms == 100hz

//epoch vars

String numString;
String packetStartEpoch;
String packetStopEpoch;


WiFiClient espClient;
PubSubClient client(espClient);

//mqtt vars 
const char* mqtt_broker = "3.12.159.20";
const char* topic = "testdb";
const int mqtt_port = 1883;

void Task1code(void* pvParameters) {
  for (;;) {
    collectData();
  }
}

unsigned long Get_Epoch_Time() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    //Serial.println("Failed to obtain time");
    return (0);
  }
  time(&now);
  return now;
}

String epochString() {
  double syncMil = millis() - startMil;
  syncMil = syncMil / 1000;
  outTime = startTime + syncMil;
  numString = String(outTime, 3 );
  //Serial.println(numString);
  return numString;
}

void audioRead() {
  i2s_read(I2S_NUM_0, i2s_data, sizeof(i2s_data), &bytes_read, portMAX_DELAY);
  for (size_t i = 0; i < numDataPoints; ++i) {
      dataArray[i] = static_cast<float>(i2s_data[i]) / 32767.0;
  }
}

//
// ZAID: The New format of the string is: # db_name, table_name, data_name, data, mac_address, start_timestamp, interval
//       This is the only function that was changed
//
String createOutMQTT(String db_name, String table_name, String data_name, float* dataArray, String macAddress, String start_timestamp, int interval) {
  String result = db_name + " ";
  result += table_name + " ";
  result += data_name + " ";
  for (int i = 0; i < numDataPoints; i++) {
    result += String(dataArray[i]);
    if (i < numDataPoints - 1) {
      result += ",";
    }
  }
  result += " " + macAddress + " ";  
  result += start_timestamp + " ";
  result += interval;
  return result;
}

void bootMQTT() {
  client.setServer(mqtt_broker, mqtt_port);
  while (!client.connected()) {

    String client_id = "esp32-client-";
    client_id += String(WiFi.macAddress());
    Serial.printf("The client %s connects to the public mqtt broker\n", client_id.c_str());
    if (client.connect(client_id.c_str())) {
      Serial.println("Public emqx mqtt broker connected");
    } else {
      Serial.print("failed with state ");

      Serial.println(client.state());
      Serial.println(client_id);
      delay(1000);
    }
  }
  client.publish(topic, "Hi EMQX I'm ESP32 ^^");
  client.setBufferSize(1023);
}


void collectData(){
  audioRead();
  if(count <= numDataPoints-1){
    if(count == 0){packetStartEpoch = epochString();} 
    rowData.audioArray[count] = dataArray[count];

    vTaskDelay(samplePeriord);
    count++;
  } 
  else if(count == numDataPoints - 1){
    rowData.audioArray[count] =  dataArray[count];
    count++;
  }
  else{ // The last index of the array
    packetStopEpoch = epochString();

    officalEnd = packetStopEpoch;
    officalStart = packetStartEpoch;

    count = 0;
    finishCollection = true;
  }
  
}

bool connectToEduroam(String InputUsername, String InputPassword) {
  bool returnStatus;
  char* ssid = "eduroam"; // eduroam SSID


  STAusername = &InputUsername[0];
  STApassword = &InputPassword[0];

  delay(10);
  Serial.println();

  WiFi.mode(WIFI_STA); //init wifi mode
  WiFi.begin(ssid, WPA2_AUTH_PEAP, STAusername, STAusername, STApassword); //without CERTIFICATE
  Serial.print(("Connecting to network: "));
  Serial.println(ssid);
  
  
  while ((WiFi.status() != WL_CONNECTED)){
    Serial.print('.');
    delay(1000);
  }
  
  // The wifi is not connected return 0
  if(WiFi.status() != WL_CONNECTED){
    returnStatus = false;
    Serial.println("Not Connected");
  }
  else{ // The wifi is connected return 1
    Serial.println(WiFi.localIP());
    Serial.println("Connected");
    returnStatus = true;
  }

  return returnStatus;
}

void setup() {
    Serial.begin(115200);
    M5.begin();

    bool connectedToWifi = connectToEduroam(EAP_IDENTITY,EAP_PASSWORD);
    Serial.println(connectedToWifi);

    // Calculates the amount of units we are measuring:
    numOfUnits = sizeof(unitArray) / sizeof(unitArray[0]);

    bootMQTT();  //starts mqtt service if needed

    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    startTime = Get_Epoch_Time();
    double tempEpoch = Get_Epoch_Time();
    while (startTime == tempEpoch) {
        startTime = Get_Epoch_Time();
    }
    startMil = millis();

    // Create a thread on core 0
    xTaskCreatePinnedToCore(
        Task1code,  // Task function
        "Task1",    // Task name
        10000,      // Stack size
        NULL,       // Task parameters
        1,          // Task priority
        &Task1,     // Task handle
        0           // Core ID (0 or 1)
    );

    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = 100,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_I2S_MSB,
        .intr_alloc_flags = 0,
        .dma_buf_count = 4,
        .dma_buf_len = 1024
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = 33,
        .ws_io_num = 32,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = 34
    };

    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);

}



void sendMQTT(String inputString) {  
  if (WiFi.status() == WL_CONNECTED) {
    if(client.connected() == true){
      const char* c = inputString.c_str();
      //inputString.toCharArray(outChar, inputString.length() + 1);
      client.publish(topic, c);
      Serial.println(inputString);
    }
    else{
      Serial.println("Broker Disconnected. Restarting ESP");
      ESP.restart();
    }
  }
  else {
    Serial.println("WiFi Disconnected. Restarting ESP");
    ESP.restart();
  }

}

void debugStatement(String officalStart, String officalEnd){
  Serial.print("Sending an MQTT Packet with the start time: ");
  Serial.print(officalStart);
  Serial.print(" and the end time: ");
  Serial.println(officalEnd);
}


//
// ZAID:  The imput parameters for createOutMQTT may need to be changed.
//
void loop() {
  if(finishCollection == true){
    Serial.println("-----------------------------------");
    int delayTime = 1000;
    debugStatement(officalStart, officalEnd);

    sendMQTT(createOutMQTT("testdb", "audio", "audiovalue", rowData.audioArray, macAddress, officalStart, samplePeriord));

    finishCollection = false;
  }
  
}
