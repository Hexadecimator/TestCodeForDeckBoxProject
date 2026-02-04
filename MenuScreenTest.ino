#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_wifi.h>
//#include "AsyncUDP.h"

void STATE_HOSTING_GAME();
void STATE_JOINING_GAME_LOOK_FOR_VALID_SSIDS();
void STATE_IN_GAME_GUEST();
void STATE_IN_GAME_HOST();

#define XPT2046_IRQ 36   // T_IRQ
#define XPT2046_MOSI 32  // T_DIN
#define XPT2046_MISO 39  // T_OUT
#define XPT2046_CLK 25   // T_CLK
#define XPT2046_CS 33    // T_CS

#define TFT_HOR_RES   240
#define TFT_VER_RES   320
#define TFT_ROTATION  LV_DISPLAY_ROTATION_90

SPIClass touchscreenSPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);
int touchX = 0;
int touchY = 0;

#define DRAW_BUF_SIZE (TFT_HOR_RES * TFT_VER_RES / 10 * (LV_COLOR_DEPTH / 8)) // 240 * 320 / 10 * (16 / 8)) = 15,360
uint8_t* draw_buf;

lv_obj_t* screen1; // home screen   [x]
lv_obj_t* screen2; // settings menu [x]
lv_obj_t* screen3; // join screen   [ ]
lv_obj_t* screen4; // host screen   [ ]
lv_obj_t* screen5; // game screen   [ ]

uint8_t baseMAC[6] = { 0, 0, 0, 0, 0, 0 };
//AsyncUDP udp;
WiFiUDP udp;
const uint16_t UDP_PORT = 6969;

#define TOTAL_PLAYER_COUNT 4
enum playerGameIDOrder { PLAYER_1, PLAYER_2, PLAYER_3, PLAYER_4, PLAYER_5, PLAYER_6, PLAYER_7, PLAYER_8 };

struct PlayerClassStruct
{
    int playerHealth = 40; // TODO: make starting life total a setting that can change
    playerGameIDOrder playerId;
};

struct __attribute__((packed)) GameStateClassStruct
{
    playerGameIDOrder Player1_ID;
    playerGameIDOrder Player2_ID;
    playerGameIDOrder Player3_ID;
    playerGameIDOrder Player4_ID;

    IPAddress Player1_IP_Address;
    uint16_t Player1_Port;

    IPAddress Player2_IP_Address;
    uint16_t Player2_Port;

    IPAddress Player3_IP_Address;
    uint16_t Player3_Port;

    IPAddress Player4_IP_Address;
    uint16_t Player4_Port;

    int player1_Health = 40;
    int player2_Health = 40;
    int player3_Health = 40;
    int player4_Health = 40;
};

// control that need to be globally available:
lv_obj_t* taGameName;
lv_obj_t * availableSSIDList;

// O=======================================================================O
// |                                                                       |
// |                             USEFUL FUNCTIONS                          |
// |                                                                       |
// O=======================================================================O

void disconnectWifi()
{
    if(WiFi.status() == WL_CONNECTED)
    {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    }
}

void readMacAddress()
{
    esp_err_t ret = esp_wifi_get_mac(WIFI_IF_STA, baseMAC);
}

String mac2String() {
  String s;
  for (byte i = 0; i < 6; ++i)
  {
    char buf[3];
    sprintf(buf, "%02X", baseMAC[i]);
    s += buf;
    if (i < 5) s += ':';
  }
  return s;
}

// O=======================================================================O
// |                                                                       |
// |                             LIST EVENTS                               |
// |                                                                       |
// O=======================================================================O

// THE JOINING PLAYER WILL ENTER THIS EVENT WHEN THEY SELECT AN SSID TO
// CONNECT TO
static void ssid_listbox_event_handler(lv_event_t *e) 
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target_obj(e);
    LV_UNUSED(obj);
    if(code == LV_EVENT_CLICKED) 
    {
        //Serial.print("Clicked: "); Serial.println(lv_list_get_button_text(availableSSIDList, obj));
        STATE_JOINING_GAME_CONNECT_TO_SSID_GUEST(lv_list_get_button_text(availableSSIDList, obj), "");
    }
}


// O=======================================================================O
// |                                                                       |
// |                             KEYBOARD EVENTS                           |
// |                                                                       |
// O=======================================================================O

static void taGameName_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = lv_event_get_target_obj(e);
    lv_obj_t * kb = (lv_obj_t *)lv_event_get_user_data(e);
    if(code == LV_EVENT_FOCUSED) 
    {
        lv_keyboard_set_textarea(kb, ta);
        lv_obj_remove_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }

    if(code == LV_EVENT_DEFOCUSED || code == LV_EVENT_READY) 
    {
        lv_keyboard_set_textarea(kb, NULL);
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
}


// O=======================================================================O
// |                                                                       |
// |                             BUTTON EVENTS                             |
// |                                                                       |
// O=======================================================================O

// O-----------------------------------------------------------------------O
// | HOME SCREEN BUTTON EVENTS
// O-----------------------------------------------------------------------O
static void btn_event_screen1JoinButton_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* btn = lv_event_get_target_obj(e);
    if(code == LV_EVENT_CLICKED)
    {
        loadScreen3();
        STATE_JOINING_GAME_LOOK_FOR_VALID_SSIDS();
    }
}

static void btn_event_screen1HostButton_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* btn = lv_event_get_target_obj(e);
    if(code == LV_EVENT_CLICKED)
    {
        loadScreen4();
        STATE_HOSTING_GAME();
    }
}

static void btn_event_screen1SettingsButton_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* btn = lv_event_get_target_obj(e);
    if(code == LV_EVENT_CLICKED)
    {
        loadScreen2();
    }
}


// O-----------------------------------------------------------------------O
// | SETTINGS SCREEN BUTTON EVENTS
// O-----------------------------------------------------------------------O
static void btn_event_screenExitToHomeButton_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* btn = lv_event_get_target_obj(e);
    if(code == LV_EVENT_CLICKED)
    {
        disconnectWifi();
        loadScreen1();
    }
}


void initScreen1() // home screen
{
    screen1 = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen1, lv_color_hex(0x0000ff), LV_PART_MAIN);

    // O-----------------------------------------------------------------------O
    // | Header label
    // O-----------------------------------------------------------------------O
    lv_obj_t* sc1Label = lv_label_create(screen1);
    lv_label_set_text(sc1Label, " - Home Screen - ");
    lv_obj_set_style_text_color(screen1, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align(sc1Label, LV_ALIGN_TOP_MID, 0, 0);

    int btnXSize = 115;
    int btnYSize = 80;
    // O-----------------------------------------------------------------------O
    // | Button 1 - Join Game Button
    // O-----------------------------------------------------------------------O
    lv_obj_t* btnJoin = lv_button_create(screen1);
    lv_obj_set_pos(btnJoin, 30, 40);
    lv_obj_set_size(btnJoin, btnXSize, btnYSize);
    lv_obj_add_event_cb(btnJoin, btn_event_screen1JoinButton_cb, LV_EVENT_ALL, NULL);

    lv_obj_t* btnJoinLabel = lv_label_create(btnJoin);
    lv_label_set_text(btnJoinLabel, "JOIN");
    lv_obj_center(btnJoinLabel);

    // O-----------------------------------------------------------------------O
    // | Button 2 - Host Game Button
    // O-----------------------------------------------------------------------O
    lv_obj_t* btnHost = lv_button_create(screen1);
    lv_obj_set_pos(btnHost, 175, 40);
    lv_obj_set_size(btnHost, btnXSize, btnYSize);
    lv_obj_add_event_cb(btnHost, btn_event_screen1HostButton_cb, LV_EVENT_ALL, NULL);

    lv_obj_t* btnHostLabel = lv_label_create(btnHost);
    lv_label_set_text(btnHostLabel, "HOST");
    lv_obj_center(btnHostLabel);

    // O-----------------------------------------------------------------------O
    // | Button 3 - Settings Menu Button
    // O-----------------------------------------------------------------------O
    lv_obj_t* btnSettings = lv_button_create(screen1);
    lv_obj_set_pos(btnSettings, 100, 140);
    lv_obj_set_size(btnSettings, btnXSize, btnYSize);
    lv_obj_add_event_cb(btnSettings, btn_event_screen1SettingsButton_cb, LV_EVENT_ALL, NULL);

    lv_obj_t* btnSettingsLabel = lv_label_create(btnSettings);
    lv_label_set_text(btnSettingsLabel, "OPTIONS");
    lv_obj_center(btnSettingsLabel);
}

void initScreen2() // settings screen
{
    screen2 = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen2, lv_color_hex(0xff0000), LV_PART_MAIN);

    // O-----------------------------------------------------------------------O
    // | Header label
    // O-----------------------------------------------------------------------O
    lv_obj_t* sc2Label = lv_label_create(screen2);
    lv_label_set_text(sc2Label, " - Settings Screen - ");
    lv_obj_set_style_text_color(screen2, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align(sc2Label, LV_ALIGN_TOP_MID, 0, 0);

    // O-----------------------------------------------------------------------O
    // | MAC Address label
    // O-----------------------------------------------------------------------O
    String tmp = mac2String();
    const char* mac = tmp.c_str();
    lv_obj_t* sc2MacAddressLabel = lv_label_create(screen2);
    lv_label_set_text(sc2MacAddressLabel, mac);
    lv_obj_set_style_text_color(screen2, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_pos(sc2MacAddressLabel, 15, 30);

    // O-----------------------------------------------------------------------O
    // | Button EXIT - Exit settings menu back to home screen
    // O-----------------------------------------------------------------------O
    lv_obj_t* btnExitSettings = lv_button_create(screen2);
    lv_obj_set_pos(btnExitSettings, 210, 180);
    lv_obj_set_size(btnExitSettings, 100, 50);
    lv_obj_add_event_cb(btnExitSettings, btn_event_screenExitToHomeButton_cb, LV_EVENT_ALL, NULL);

    lv_obj_t* btnExitSettingsLabel = lv_label_create(btnExitSettings);
    lv_label_set_text(btnExitSettingsLabel, "EXIT");
    lv_obj_center(btnExitSettingsLabel);

    // O-----------------------------------------------------------------------O
    // | Hosted Game Name text area
    // | Note: Keyboards need to initialize last so they appear over the top
    // |       of the other controls
    // O-----------------------------------------------------------------------O
    lv_obj_t* kbGameName = lv_keyboard_create(screen2);
    //lv_obj_t* taGameName; // moved to global
    taGameName = lv_textarea_create(screen2);
    lv_obj_set_pos(taGameName, 15, 55);
    lv_obj_add_event_cb(taGameName, taGameName_event_cb, LV_EVENT_ALL, kbGameName);
    lv_textarea_set_placeholder_text(taGameName, "Enter Host Name");
    lv_obj_set_size(taGameName, 150, 40);
    lv_obj_add_flag(kbGameName, LV_OBJ_FLAG_HIDDEN);
}

void initScreen3() // join game screen
{
    screen3 = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen3, lv_color_hex(0x00ff00), LV_PART_MAIN);

    // O-----------------------------------------------------------------------O
    // | Header label
    // O-----------------------------------------------------------------------O
    lv_obj_t* sc3Label = lv_label_create(screen3);
    lv_label_set_text(sc3Label, " - Join Game Screen - ");
    lv_obj_set_style_text_color(screen3, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align(sc3Label, LV_ALIGN_TOP_MID, 0, 0);

    // O-----------------------------------------------------------------------O
    // | Button EXIT - Exit settings menu back to home screen
    // O-----------------------------------------------------------------------O
    lv_obj_t* btnExitSettings = lv_button_create(screen3);
    lv_obj_set_pos(btnExitSettings, 220, 190);
    lv_obj_set_size(btnExitSettings, 90, 40);
    lv_obj_add_event_cb(btnExitSettings, btn_event_screenExitToHomeButton_cb, LV_EVENT_ALL, NULL);

    lv_obj_t* btnExitSettingsLabel = lv_label_create(btnExitSettings);
    lv_label_set_text(btnExitSettingsLabel, "EXIT");
    lv_obj_center(btnExitSettingsLabel);
}

void initScreen4() // host game screen
{
    screen4 = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen4, lv_color_hex(0x00ff00), LV_PART_MAIN);

    // O-----------------------------------------------------------------------O
    // | Header label
    // O-----------------------------------------------------------------------O
    lv_obj_t* sc4Label = lv_label_create(screen4);
    lv_label_set_text(sc4Label, " - Host Game Screen - ");
    lv_obj_set_style_text_color(screen4, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align(sc4Label, LV_ALIGN_TOP_MID, 0, 0);

    // O-----------------------------------------------------------------------O
    // | Button EXIT - Exit settings menu back to home screen
    // O-----------------------------------------------------------------------O
    lv_obj_t* btnExitSettings = lv_button_create(screen4);
    lv_obj_set_pos(btnExitSettings, 220, 190);
    lv_obj_set_size(btnExitSettings, 90, 40);
    lv_obj_add_event_cb(btnExitSettings, btn_event_screenExitToHomeButton_cb, LV_EVENT_ALL, NULL);

    lv_obj_t* btnExitSettingsLabel = lv_label_create(btnExitSettings);
    lv_label_set_text(btnExitSettingsLabel, "EXIT");
    lv_obj_center(btnExitSettingsLabel);
}

void initScreen5() // in-game screen
{
    screen5 = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen5, lv_color_hex(0x00ff00), LV_PART_MAIN);
}

void initScreens()
{
    initScreen1();
    initScreen2();
    initScreen3();
    initScreen4();
    initScreen5();
}

void loadScreen1()
{
    lv_screen_load(screen1);
}

void loadScreen2()
{
    lv_screen_load(screen2);
}

void loadScreen3()
{
    lv_screen_load(screen3);
}

void loadScreen4()
{
    lv_screen_load(screen4);
}

void loadScreen5()
{
    lv_screen_load(screen5);
}


void my_disp_flush( lv_display_t *disp, const lv_area_t *area, uint8_t * px_map)
{
    lv_display_flush_ready(disp);
}

/*Read the touchpad*/
void my_pointer_read( lv_indev_t * indev, lv_indev_data_t * data )
{
    if (touchscreen.touched())
    {
        TS_Point p = touchscreen.getPoint();
        touchX = map(p.x, 200, 3700, 1, TFT_HOR_RES);
        touchY = map(p.y, 240, 3800, 1, TFT_VER_RES);
        data->point.x = touchX;
        data->point.y = touchY;
        data->state = LV_INDEV_STATE_PR;
    }
    else
    {
        data->state = LV_INDEV_STATE_REL;
    }
}

static uint32_t my_tick(void)
{
    return millis();
}

void setup()
{
    String LVGL_Arduino = "Hello Arduino! ";
    LVGL_Arduino += String('V') + lv_version_major() + "." + lv_version_minor() + "." + lv_version_patch();

    Serial.begin( 115200 );
    Serial.println( LVGL_Arduino );

    WiFi.mode(WIFI_STA);
    WiFi.STA.begin();
    readMacAddress();

    lv_init();

    /*Set a tick source so that LVGL will know how much time elapsed. */
    lv_tick_set_cb(my_tick);

    draw_buf = new uint8_t[DRAW_BUF_SIZE];
    lv_display_t * disp;
    disp = lv_tft_espi_create(TFT_HOR_RES, TFT_VER_RES, draw_buf, DRAW_BUF_SIZE);
    lv_display_set_rotation(disp, TFT_ROTATION);

    touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    touchscreen.begin(touchscreenSPI);
    touchscreen.setRotation(2);

    /*Initialize the (dummy) input device driver*/
    lv_indev_t * indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER); /*Touchpad should have POINTER type*/
    lv_indev_set_read_cb(indev, my_pointer_read);

    initScreens(); // create all screens
    loadScreen1(); // load home screen

    Serial.println( "Setup done, entering loop" );
}



// O=======================================================================O
// |                                                                       |
// |                       GAME STATES                                     |
// |                                                                       |
// O=======================================================================O

const char* prefix = "box";

bool ssidStartsWithPrefix(const char* s)
{
    return s && strncmp(s, "box", 3) == 0;
}

void STATE_HOSTING_GAME()
{
    // TODO: Check if no game name and force them to name game
    const char* ssid = lv_textarea_get_text(taGameName);
    const char* prefix = "box";
    size_t len = strlen(prefix) + strlen(ssid) + 1;
    char* ssidWithPrefix = new char[len];
    strcpy(ssidWithPrefix, prefix);
    strcat(ssidWithPrefix, ssid);

    const char* password = "";

    disconnectWifi();
    WiFi.softAP(ssidWithPrefix, password);

    udp.begin(UDP_PORT);

    STATE_JOINING_GAME_HOST_NEGOTIATE_WITH_GUESTS();
}

// FIND ALL VALID SSIDS AND DISPLAY THEM ON A LIST
// CLICKING AN SSID ON THE LIST WILL FIRE AN EVENT
// TO CONNECT TO THE SSID AND START THE GAME LOOP
void STATE_JOINING_GAME_LOOK_FOR_VALID_SSIDS()
{
    disconnectWifi();
    int n = WiFi.scanNetworks();
    int boxSsids = 0;
    for (int i = 0; i < n; i++)
    {
        if(ssidStartsWithPrefix(WiFi.SSID(i).c_str())) boxSsids++;
    }

    if(boxSsids == 0)
    {
        // Error message to show the user that no boxes could
        // be detected in the vicinity
        lv_obj_t* errorLabel = lv_label_create(lv_screen_active());
        lv_label_set_text(errorLabel, "Oh no! No other boxes detected! Try re-scan");
        lv_obj_set_style_text_color(lv_screen_active(), lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_align(errorLabel, LV_ALIGN_LEFT_MID, 0, 0);

        disconnectWifi();
        return;
    }

    String SSIDList[boxSsids];
    int idx = 0;
    for (int i = 0; i < n; i++)
    {
        if(ssidStartsWithPrefix(WiFi.SSID(i).c_str()))
        {
            SSIDList[idx] = WiFi.SSID(i);
            idx++;
        }
    }

    lv_obj_t * availableSSIDList;
    availableSSIDList = lv_list_create(lv_screen_active());
    lv_obj_set_pos(availableSSIDList, 10, 25);
    lv_obj_set_size(availableSSIDList, 300, 160);

    for(int i = 0; i < boxSsids; i++)
    {
        lv_obj_t *list_btn = lv_list_add_btn(availableSSIDList, LV_SYMBOL_RIGHT, SSIDList[i].c_str());
        lv_obj_add_event_cb(list_btn, ssid_listbox_event_handler, LV_EVENT_CLICKED, NULL);
    }
}

// SSID has been chosen, now connect to host
// and establish player ID 
void STATE_JOINING_GAME_CONNECT_TO_SSID_GUEST(const char* chosenSSID, const char* password)
{
    WiFi.begin(chosenSSID, password);
    int tryCount = 0;
    int numTries = 10;
    while(WiFi.status() != WL_CONNECTED && tryCount <= numTries)
    {
        delay(100);
        tryCount++;
    }

    if(tryCount >= numTries)
    {
        //TODO: Connection failure message on screen
        Serial.print("Could not connect to SSID ");
        Serial.println(chosenSSID);
    }
    else
    {
        STATE_JOINING_GAME_GUEST_NEGOTIATE_WITH_HOST();
    }
}

const char* helloDiscoveryMessage = "requestJoin";
void STATE_JOINING_GAME_GUEST_NEGOTIATE_WITH_HOST()
{
    
}

void STATE_JOINING_GAME_HOST_NEGOTIATE_WITH_GUESTS()
{
    // to get a senders IP address and port:
    //IPAddress remote = udp.remoteIP();
    //uint16_t port = udp.remotePort();

    // TODO: Invent some kind of "hello" discovery packet (started above with helloDiscoveryMessage)
    // 1. The host sits and waits in a loop, ready to parse any packets it receives. First it checks if a message is a known size, if not it tries to parse it as a string
    // 2. The guest, once connected to the SSID, immediately sends the host a helloDiscoveryMessage (maybe in a loop sending it multiple times)
    // 3. The host grabs the guest's IPAddress and Port and assigns the player a sequentially-chosen player ID (from the enum... up to 8 players) 
    // -----> The host is keeping a count of total joined players at this time
    // -----> The host will always be PLAYER_1
    // 4. Once the guest has been assigned its player ID, it moves to the GUEST_GAME_LOOP where it will send its life total to the host periodically 
    // 5. Once 4 total players have joined the game, the host will move to the HOST_GAME_LOOP
    // 6. Inside the HOST_GAME_LOOP, the host will collect status from each player, update the gamestate struct, and send out the gamestate struct to the guests

    bool exit_loop = false;
    bool game_started = false;
    unsigned long startTime = millis();
    unsigned long timeOut = 60000;
    int numJoinedPlayers = 0;
    char rxBuffer[128];
    while(!game_started && !exit_loop)
    {
        // negotiation packets will essentially be string-based
        int packetSize = udp.parsePacket();
        if(packetSize > 0)
        {            
            int len = udp.read(rxBuffer, sizeof(rxBuffer) - 1);
            buffer[len] = '\0';
            // buffer is now effectively a string with a null terminator
            String.print("HOST_NEGOTIATING: Recieved packet [");
            String.print(buffer);
            String.println("]");

            // TODO: compare the contents of buffer with predefined messages
            // if a guest is saying hello, sequentially assign them player ID
            // (while the host records IPAddress and port of player)
            // until 4 guests total have joined
            // once 4 guests have joined, the game loop can be entered
        }

        if(millis() - startTime >= timeOut) exit_loop = true;

        if(numJoinedPlayers >= TOTAL_PLAYER_COUNT) game_started = true;
    }

    if(exit_loop) 
    {
        // bummer, handle the timeout
        return;
    }

    if(game_started)
    {
        // move to the HOST_GAME_LOOP
    }

}

void STATE_JOINING_GAME_HOST()
{

}

void STATE_IN_GAME_GUEST()
{

}

void STATE_IN_GAME_HOST()
{

}

void loop()
{
    lv_timer_handler(); /* let the GUI do its work */
    delay(5); /* let this time pass */
}
