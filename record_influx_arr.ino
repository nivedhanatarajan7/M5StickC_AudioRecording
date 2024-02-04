#include <M5StickC.h>
#include <driver/i2s.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "time.h"

const char* EAP_IDENTITY = "user";
const char* EAP_PASSWORD = "password";

unsigned long packetStartEpoch;

WiFiClient wifiClient;

const long gmtOffset_sec = 0;
const int daylightOffset_sec = 0;

unsigned long epochTimestamp() {
    time_t now;
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return 0;
    }
    time(&now);
    return now;
}

void sendToInfluxDB(float dataArray[], size_t arraySize, unsigned long timestamp) {
    HTTPClient http;
    String url = "https://sensorweb.us:8086/write?db=testdb";
    String dataStr = "";

    for (size_t i = 0; i < arraySize; ++i) {
        dataStr += "audio audiovalue=" + String(dataArray[i], 2) + " " + timestamp;
        if (i < arraySize - 1) {
            dataStr += "\n";
        }
        timestamp += 1;
    }

    http.begin(url);
    http.setAuthorization("test", "sensorweb");
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    int httpResponseCode = http.POST(dataStr);

    if (httpResponseCode == 204) {
        Serial.println("Data sent to InfluxDB successfully.");
        Serial.println(dataStr);
    } else {
        Serial.print("Error sending data to InfluxDB. Status code: ");
        Serial.println(httpResponseCode);
        String response = http.getString();
        Serial.println("Response: " + response);
    }

    http.end();
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
    configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org");

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

void loop() {
    size_t arraySize = 2;
    float dataArray[arraySize];

    size_t bytes_read;
    uint16_t i2s_data[arraySize];

    packetStartEpoch = epochTimestamp();
    while (1) {
        i2s_read(I2S_NUM_0, i2s_data, sizeof(i2s_data), &bytes_read, portMAX_DELAY);
        for (size_t i = 0; i < arraySize; ++i) {
            dataArray[i] = static_cast<float>(i2s_data[i]) / 32767.0;
        }
        sendToInfluxDB(dataArray, arraySize, packetStartEpoch);
        packetStartEpoch += arraySize;
        delay(100); 
    }
}
