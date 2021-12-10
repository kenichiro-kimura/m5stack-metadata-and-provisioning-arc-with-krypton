#include <M5Stack.h>
#include <WireGuard-ESP32.h>
#define TINY_GSM_MODEM_UBLOX
#include <TinyGsmClient.h>
#include <HTTPClient.h>
#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>

StaticJsonDocument<256> json_request;
char buffer[512];
char ssid[32];
char password[32];

TinyGsm modem(Serial2); /* 3G board modem */
TinyGsmClient socket(modem);
TinyGsmClientSecure client(modem);
WireGuard wg;

void setup() {
  M5.begin();
  M5.Power.begin();
  M5.Lcd.clear(BLACK);
  M5.Lcd.setTextColor(WHITE);

  M5.Lcd.println(F("M5Stack + 3G Module"));

  M5.Lcd.print(F("modem.restart()"));
  Serial2.begin(115200, SERIAL_8N1, 16, 17);
  modem.restart();
  M5.Lcd.println(F("done"));

  M5.Lcd.print(F("getModemInfo:"));
  String modemInfo = modem.getModemInfo();
  M5.Lcd.println(modemInfo);

  M5.Lcd.print(F("waitForNetwork()"));
  while (!modem.waitForNetwork()) M5.Lcd.print(".");
  M5.Lcd.println(F("Ok"));

  M5.Lcd.print(F("gprsConnect(soracom.io)"));
  modem.gprsConnect("soracom.io", "sora", "sora");
  M5.Lcd.println(F("done"));

  M5.Lcd.print(F("isNetworkConnected()"));
  while (!modem.isNetworkConnected()) M5.Lcd.print(".");
  M5.Lcd.println(F("Ok"));

  M5.Lcd.print(F("My IP addr: "));
  IPAddress ipaddr = modem.localIP();
  M5.Lcd.println(ipaddr);

  M5.Lcd.println(F("justify timer by ntp.."));
  configTime(9 * 60 * 60, 0, "ntp.jst.mfeed.ad.jp", "ntp.nict.jp", "time.google.com");
  
  //get wifi information from metadata
  HttpClient http = HttpClient(socket,"metadata.soracom.io",80);
  M5.Lcd.println(F("get wifi information from soracom metadata service.."));
  http.get("/v1/userdata");
  int status_code = http.responseStatusCode();
  String response_body = http.responseBody(); 
  M5.Lcd.printf("http status:%d\n", status_code);

  if(status_code == 200){
    StaticJsonDocument<128> json_response;
    deserializeJson(json_response, response_body);
    const char* json_ssid = json_response["ssid"];
    const char* json_password = json_response["password"];
    if(json_ssid == NULL || json_password == NULL){
      M5.Lcd.println(F("metadata is invalid"));
      while(1);
    }
    strcpy(ssid,json_ssid);
    strcpy(password,json_password);
  } else {
      M5.Lcd.println(F("fail to get wifi information"));    
      while(1);
  }
  http.stop();

  M5.Lcd.println(F("Connecting to the AP..."));
  WiFi.begin(ssid, password);
  while( !WiFi.isConnected() ) {
    M5.Lcd.print(F("."));
    delay(1000);
  }
  M5.Lcd.println(F("Connected."));
  
  HttpClient https = HttpClient(client,"krypton.soracom.io",8036);
  M5.Lcd.println(F("provisioning soracom arc by soracom krypton...."));
  https.connectionKeepAlive();  
  sprintf(buffer,"{}");
  https.post("/v1/provisioning/soracom/arc/bootstrap","application/json",buffer);
  status_code = https.responseStatusCode();
  response_body = https.responseBody(); 
  M5.Lcd.printf("http status:%d\n", status_code);
  
  if( status_code == 200 ){
    StaticJsonDocument<512> json_response;
    deserializeJson(json_response, response_body);

    const char* arcClientPeerIpAddress = json_response["arcClientPeerIpAddress"];
    const char* arcClientPeerPrivateKey = json_response["arcClientPeerPrivateKey"];
    const char* arcServerEndpoint = json_response["arcServerEndpoint"];
    const char* arcServerPeerPublicKey = json_response["arcServerPeerPublicKey"];

    // get local ip address
    int octet[4];
    strcpy(buffer,arcClientPeerIpAddress);
    char* tok = strtok(buffer,".");
    octet[0] = atoi(tok);
    tok = strtok(NULL,".");
    octet[1] = atoi(tok);
    tok = strtok(NULL,".");
    octet[2] = atoi(tok);
    tok = strtok(NULL,".");
    octet[3] = atoi(tok);
    IPAddress local_ip(octet[0],octet[1],octet[2],octet[3]);

    // get endpoint name and port
    int endpoint_port;
    char endpoint_address[32];
    strcpy(buffer,arcServerEndpoint);
    tok = strtok(buffer,":");
    strcpy(endpoint_address,tok);
    tok = strtok(NULL,":");
    endpoint_port = atoi(tok);
           
    M5.Lcd.println(F("Initializing WireGuard..."));
    wg.begin(
      local_ip,
      arcClientPeerPrivateKey,
      endpoint_address,
      arcServerPeerPublicKey,
      endpoint_port);    
  } else {
    M5.Lcd.println(F("failed to provisioning soracom arc.."));
    M5.Lcd.println(response_body);
    while(1);
  }
  https.stop();
  
  // stop 3G Modem
  M5.Lcd.println(F("try to stop modem..."));
  modem.poweroff();
  M5.Lcd.println(F("modem power off"));  
  
  delay(1000);
}

int i = 0;
void loop() {
  HTTPClient http;
  i++;
  sprintf(buffer,"{\"counter\":%d}",i);
  http.begin("http://uni.soracom.io/");
  http.addHeader("Content-Type","application.json");
  int status_code = http.POST(buffer);
  http.end();

  M5.Lcd.clear();
  M5.Lcd.setCursor(0,0);
  M5.Lcd.printf("http status:%d\nsend data:%s\n",status_code,buffer);
  delay(5000);
}
