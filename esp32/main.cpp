#include <Arduino.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <Wire.h>
#include <Digital_Light_TSL2561.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "esp_wpa2.h"
#include "esp_wifi.h"
#include <HTTPClient.h>

#define BUZZPIN 5
#define DHT22_PIN 23

DHT dht22(DHT22_PIN, DHT22);

typedef struct Data
{
    float temp;
    float hum;
    float light;
} Data;


TaskHandle_t task01;
TaskHandle_t task02;

typedef enum WarningType {NONE, TEMPERATURE, HUMIDITY, LIGHT} WarningType;
WarningType warningType;

typedef enum WifiNetwork {FAILED, EDUROAM, HOME} WifiNetwork;
WifiNetwork wifiNetwork;
WifiNetwork failedNetwork;

void WarnUser(void *parameter)
{

    while(1)
    {
		switch(warningType)
		{
			case TEMPERATURE:
			case HUMIDITY:
			case LIGHT:
			{
				digitalWrite(BUZZPIN, HIGH);
				vTaskDelay(2000 / portTICK_PERIOD_MS);
				digitalWrite(BUZZPIN, LOW);
				break;
			}
			case NONE:
				break;
		}
		vTaskDelay(2000 / portTICK_PERIOD_MS);
		}
}

void InitWifi(const char *ssid, const char *identity, const char *password, const char *ssidHome, const char *passwordHome)
{   

    bool triedToConnect = false;

    uint32_t startMillis;
    uint32_t currentMillis;

    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);

    startMillis = millis();
	Serial.print("Connecting to network");

	wifiNetwork = EDUROAM;
    while(WiFi.status() != WL_CONNECTED)
    {   
        currentMillis = millis();
		if(currentMillis - startMillis >= 11000)
		{              
			failedNetwork = wifiNetwork;
			wifiNetwork = FAILED;
			startMillis = currentMillis;
		}
        switch(wifiNetwork)
        {	
            case EDUROAM:
            {
				Serial.println("Connecting to Eduroam");
                if(!triedToConnect)
        	    {
                    esp_wifi_sta_wpa2_ent_set_identity((uint8_t*)identity, strlen(identity));
                    esp_wifi_sta_wpa2_ent_set_username((uint8_t*)identity, strlen(identity));
                    esp_wifi_sta_wpa2_ent_set_password((uint8_t*)password, strlen(password));
                    esp_wifi_sta_wpa2_ent_enable();
                    WiFi.begin(ssid);
					triedToConnect = true;
                }
				break;
            }
            case HOME:
            {   
				Serial.println("Connecting to Home");
                if(!triedToConnect)
                {
                    WiFi.begin(ssidHome, passwordHome);
					triedToConnect = true;
                }
				break;
            }
            case FAILED:
            {	
				Serial.println("switching network");
				triedToConnect = false;
				WiFi.disconnect(true);
                if(failedNetwork == EDUROAM)
				{
                    wifiNetwork = HOME;
					esp_wifi_sta_wpa2_ent_disable();
				}
				else if(failedNetwork == HOME)
                {
					wifiNetwork = EDUROAM;
				}
				break;          
            }
      	}
		vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
  

}

void InitSensors(void)
{
	Wire.begin();
	TSL2561.init();
	dht22.begin();
	pinMode(BUZZPIN, OUTPUT);
	Serial.println("");
}

String GetResponse(int httpResponseCode, HTTPClient *http)
{
  if(httpResponseCode > 0)
    return http->getString();
    
  else
    return "Error: Request Failed";
}

void GenerateAndSendOverHTTP(void *parameter)
{
	char serverUrl[45];
	switch(wifiNetwork)
	{
		case EDUROAM:
		{
			strcpy(serverUrl,"http://145.93.169.11:5000/write-data");
			break;
		}
		case HOME:
		{
			strcpy(serverUrl,"http://192.168.0.158:5000/write-data");
			break;
		}
	}
	while(1)
	{
		Data data;
		data.temp = dht22.readTemperature();
		data.hum = dht22.readHumidity();
		data.light = TSL2561.readVisibleLux();
    Serial.print("Temp:");
    Serial.println(data.temp);
    Serial.print("Humidity:");
    Serial.println(data.hum);
    Serial.print("Light:");
    Serial.println(data.light);


		// Turn On/Off Warning
		if(data.temp > 25)
			warningType = TEMPERATURE;
		else if(data.hum > 70)
			warningType = HUMIDITY;
		else if(data.light > 5000)
			warningType = LIGHT;
		else
			warningType = NONE;

		if(WiFi.status() == WL_CONNECTED)
		{
			HTTPClient http;
			HTTPClient *ptrHttp = &http;
			http.begin(serverUrl);
			http.addHeader("Content-Type", "application/json");
			String jsonStr;
			uint8_t httpResponseCode;

			StaticJsonDocument<200> jsonDoc;
			jsonDoc["Temperature"] = data.temp;
			jsonDoc["Humidity"] = data.hum;
			jsonDoc["Light"] = data.light;
	
			serializeJson(jsonDoc, jsonStr);
			jsonDoc.clear();
			httpResponseCode = http.POST(jsonStr);
			Serial.println(httpResponseCode);
			jsonStr.clear();
			
			Serial.println("Response: " + GetResponse(httpResponseCode, ptrHttp));
		
		
		if(warningType != NONE)
		{
			switch(warningType)
			{
				case TEMPERATURE:
				{
				jsonDoc["warning"] = "Temperature is too high!";
				break;
				}
				case HUMIDITY:
				{
				jsonDoc["warning"] = "Humidity is too high!";
				break;
				}
				case LIGHT:
				{
				jsonDoc["warning"] = "Light intensity is too high!";
				break;
				}
				case NONE:
				break;
			}
			serializeJson(jsonDoc, jsonStr);
			httpResponseCode = http.POST(jsonStr);
			Serial.println("Response: " + GetResponse(httpResponseCode, ptrHttp));
		}
		

		http.end();
		vTaskDelay(10000 / portTICK_PERIOD_MS);
		}
	}
}

void setup()
{
  warningType = NONE;

  // Init serial communication
  Serial.begin(115200);

  InitWifi("name01", "email", "password", "name02", "password2");

  // Print Local IP
  Serial.println(WiFi.localIP());
  Serial.println(WiFi.status());
  InitSensors();
  xTaskCreatePinnedToCore(GenerateAndSendOverHTTP, "Flask", 4096, NULL, 1, &task01, 0);
  xTaskCreatePinnedToCore(WarnUser, "Warning", 1000, NULL, 1, &task02, 1);
}

void loop()
{

}
