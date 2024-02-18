#include <M5StickC.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "time.h"
#include "esp_wpa2.h"
#include <driver/i2s.h>

char* EAP_IDENTITY = "user";
char* EAP_PASSWORD = "pass";

char* STAusername;
char* STApassword;

String macAddress = WiFi.macAddress();

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
  "audio",
};

int numOfUnits;
int count;
bool finishCollection = false;

int samplePeriord = 1; // 10 ms == 100hz

String numString;
String packetStartEpoch;
String packetStopEpoch;

WiFiClient espClient;
PubSubClient client(espClient);

const char* mqtt_broker = "3.12.159.20";
const char* topic = "shake";
const int mqtt_port = 1883;

unsigned long Get_Epoch_Time() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return 0;
  }
  time(&now);
  return now;
}

String epochString(int displace) {
  double syncMil = millis() - startMil;
  syncMil = syncMil / 1000 + displace;
  outTime = startTime + syncMil;
  numString = String(outTime, 3);
  return numString;
}

void audioRead() {
  i2s_read(I2S_NUM_0, i2s_data, sizeof(i2s_data), &bytes_read, portMAX_DELAY);
  for (size_t i = 0; i < numDataPoints; ++i) {
    dataArray[i] = static_cast<float>(i2s_data[i]) / 32767.0;
  }
}

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

void collectData() {
  audioRead();
  if (count <= numDataPoints - 1) {
    if (count == 0) {
      packetStartEpoch = epochString(-10);
    }
    rowData.audioArray[count] = dataArray[count];
    delay(samplePeriord);
    count++;
  } else if (count == numDataPoints - 1) {
    rowData.audioArray[count] = dataArray[count];
    count++;
  } else { // The last index of the array
    packetStopEpoch = epochString(1);
    officalEnd = packetStopEpoch;
    officalStart = packetStartEpoch;
    count = 0;
    finishCollection = true;
  }
}

bool connectToEduroam(String InputUsername, String InputPassword) {
  char* ssid = "eduroam"; // eduroam SSID
  STAusername = &InputUsername[0];
  STApassword = &InputPassword[0];
  delay(10);
  Serial.println();
  WiFi.mode(WIFI_STA); // init wifi mode
  WiFi.begin(ssid, WPA2_AUTH_PEAP, STAusername, STAusername, STApassword); // without CERTIFICATE
  Serial.print(("Connecting to network: "));
  Serial.println(ssid);
  while ((WiFi.status() != WL_CONNECTED)) {
    Serial.print('.');
    delay(1000);
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Not Connected");
    return false;
  } else {
    Serial.println(WiFi.localIP());
    Serial.println("Connected");
    return true;
  }
}

void setup() {
  Serial.begin(115200);
  M5.begin();
  bool connectedToWifi = connectToEduroam(EAP_IDENTITY, EAP_PASSWORD);
  Serial.println(connectedToWifi);
  numOfUnits = sizeof(unitArray) / sizeof(unitArray[0]);
  bootMQTT(); // starts mqtt service if needed
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  startTime = Get_Epoch_Time();
  double tempEpoch = Get_Epoch_Time();
  while (startTime == tempEpoch) {
    startTime = Get_Epoch_Time();
  }
  
  startMil = millis();
  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = 44100,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = I2S_COMM_FORMAT_I2S_MSB,
      .intr_alloc_flags = 0,
      .dma_buf_count = 4,
      .dma_buf_len = 1024};
  i2s_pin_config_t pin_config = {
      .bck_io_num = 33,
      .ws_io_num = 32,
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = 34};
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
}

void sendMQTT(String inputString) {
  if (WiFi.status() == WL_CONNECTED) {
    if (client.connected() == true) {
      const char* c = inputString.c_str();
      client.publish(topic, c);
      Serial.println(inputString);
    } else {
      Serial.println("Broker Disconnected. Restarting ESP");
      ESP.restart();
    }
  } else {
    Serial.println("WiFi Disconnected. Restarting ESP");
    ESP.restart();
  }
}

void debugStatement(String officalStart, String officalEnd) {
  Serial.print("Sending an MQTT Packet with the start time: ");
  Serial.print(officalStart);
  Serial.print(" and the end time: ");
  Serial.println(officalEnd);
}

void loop() {
  collectData();
  if (finishCollection) {
    Serial.println("-----------------------------------");
    int delayTime = 1000;
    debugStatement(officalStart, officalEnd);
    sendMQTT(createOutMQTT("shake", "audio", "audiovalue", rowData.audioArray, macAddress, officalStart, samplePeriord));
    finishCollection = false;
  }
  // delay(100); // Adjust the delay based on your requirements
}
