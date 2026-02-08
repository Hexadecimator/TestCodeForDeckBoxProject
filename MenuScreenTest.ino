#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_wifi.h>


// O-----------------------------------------------------------------------O
// | Sloppy function instantiations
// O-----------------------------------------------------------------------O
void STATE_HOSTING_GAME();
void STATE_JOINING_GAME_LOOK_FOR_VALID_SSIDS();
void STATE_IN_GAME_GUEST();
void STATE_IN_GAME_HOST();


// O-----------------------------------------------------------------------O
// | Screen related variables
// O-----------------------------------------------------------------------O
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
lv_obj_t* screen3; // join screen   [x]
lv_obj_t* screen4; // host screen   [x]
lv_obj_t* screen5; // game screen   [ ]

// Screen controls that need to be globally available:
lv_obj_t* taGameName;
lv_obj_t * availableSSIDList;

lv_obj_t* sc5Player1HealthValueLabel;
lv_obj_t* sc5Player2HealthValueLabel;
lv_obj_t* sc5Player3HealthValueLabel;
lv_obj_t* sc5Player4HealthValueLabel;

// O-----------------------------------------------------------------------O
// | WiFi related variables
// O-----------------------------------------------------------------------O
uint8_t baseMAC[6] = { 0, 0, 0, 0, 0, 0 };
WiFiUDP udp;
const uint16_t UDP_PORT = 6969;
IPAddress resetAddress(0, 0, 0, 0);


// O-----------------------------------------------------------------------O
// | Game state related variables
// O-----------------------------------------------------------------------O
#define TOTAL_PLAYER_COUNT 2

int startingLifeTotal = 40;

bool IN_GAME_GUEST = false;
bool IN_GAME_HOST = false;

bool decreaseHealthButtonClicked = false;
bool increaseHealthButtonClicked = false;

enum playerGameIDOrder 
{ 
    NOT_PLAYING = 0, 
    PLAYER_1 = 1, 
    PLAYER_2 = 2, 
    PLAYER_3 = 3, 
    PLAYER_4 = 4, 
    PLAYER_5 = 5, 
    PLAYER_6 = 6, 
    PLAYER_7 = 7, 
    PLAYER_8 = 8 
};

struct PlayerClassStruct
{
    int playerHealth = startingLifeTotal;
    int playerPoison = 0; // just example
    playerGameIDOrder playerId = playerGameIDOrder::NOT_PLAYING;
    IPAddress HostIPAddress;
};

struct GameHostStruct
{
    // host is always player ID #1
    playerGameIDOrder Player1_ID = playerGameIDOrder::PLAYER_1;
    playerGameIDOrder Player2_ID = playerGameIDOrder::NOT_PLAYING;
    playerGameIDOrder Player3_ID = playerGameIDOrder::NOT_PLAYING;
    playerGameIDOrder Player4_ID = playerGameIDOrder::NOT_PLAYING;  

    IPAddress Player1_IP_Address;
    uint16_t Player1_Port = 0;

    IPAddress Player2_IP_Address;
    uint16_t Player2_Port = 0;

    IPAddress Player3_IP_Address;
    uint16_t Player3_Port = 0;

    IPAddress Player4_IP_Address;
    uint16_t Player4_Port = 0;
};

struct __attribute__((packed)) GameStateStruct
{
    int player1_Health = startingLifeTotal;
    int player2_Health = startingLifeTotal;
    int player3_Health = startingLifeTotal;
    int player4_Health = startingLifeTotal;

    // what else?
    // 1. turn order? but they'd have to remember to pass turn on the device that's not happening
    // 2. commander damage? how to do that?
    // 3. other permanent statuses like poison
};

// Persistent Game State Data
PlayerClassStruct GSD_PlayerDataStruct;
GameHostStruct    GSD_HostGameStruct;
GameStateStruct   GSD_GameStateStruct;


// O=======================================================================O
// |                                                                       |
// |                             USEFUL FUNCTIONS                          |
// |                                                                       |
// O=======================================================================O

void reset_GSD_PlayerDataStruct()
{
    GSD_PlayerDataStruct.playerHealth = startingLifeTotal;
    GSD_PlayerDataStruct.playerPoison = 0;
    GSD_PlayerDataStruct.playerId = playerGameIDOrder::NOT_PLAYING;
    GSD_PlayerDataStruct.HostIPAddress = resetAddress;
}

void reset_GSD_HostGameStruct()
{
    GSD_HostGameStruct.Player1_ID = playerGameIDOrder::NOT_PLAYING;
    GSD_HostGameStruct.Player2_ID = playerGameIDOrder::NOT_PLAYING;
    GSD_HostGameStruct.Player3_ID = playerGameIDOrder::NOT_PLAYING;
    GSD_HostGameStruct.Player4_ID = playerGameIDOrder::NOT_PLAYING;

    GSD_HostGameStruct.Player1_IP_Address = resetAddress;
    GSD_HostGameStruct.Player2_IP_Address = resetAddress;
    GSD_HostGameStruct.Player3_IP_Address = resetAddress;
    GSD_HostGameStruct.Player4_IP_Address = resetAddress;

    GSD_HostGameStruct.Player1_Port = 0;
    GSD_HostGameStruct.Player2_Port = 0;
    GSD_HostGameStruct.Player3_Port = 0;
    GSD_HostGameStruct.Player4_Port = 0;
}

void reset_GSD_GameStateStruct()
{
    GSD_GameStateStruct.player1_Health = startingLifeTotal;
    GSD_GameStateStruct.player2_Health = startingLifeTotal;
    GSD_GameStateStruct.player3_Health = startingLifeTotal;
    GSD_GameStateStruct.player4_Health = startingLifeTotal;
}

void resetAllGameStateStructs()
{
    reset_GSD_PlayerDataStruct();
    reset_GSD_HostGameStruct();
    reset_GSD_GameStateStruct();
}

void delaySafeMilli(unsigned long timeToWait)
{
    unsigned long start = millis();
    while((millis() - start) <= timeToWait) { /* do nothing but don't block */ }
}

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

String mac2String() 
{
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
        // TODO: Password handling
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


// O-----------------------------------------------------------------------O
// | GAME SCREEN (5) BUTTON EVENTS
// O-----------------------------------------------------------------------O
static void btn_event_screen5DecreaseHealthButton_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* btn = lv_event_get_target_obj(e);
    if(code == LV_EVENT_CLICKED)
    {
        decreaseHealthButtonClicked = true;
    }
}

static void btn_event_screen5IncreaseHealthButton_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* btn = lv_event_get_target_obj(e);
    if(code == LV_EVENT_CLICKED)
    {
        increaseHealthButtonClicked = true;
    }
}

// O=======================================================================O
// |                                                                       |
// |                             SCREEN INITIALIZATIONS                    |
// |                                                                       |
// O=======================================================================O

// O-----------------------------------------------------------------------O
// | SCREEN 1 - HOME SCREEN
// O-----------------------------------------------------------------------O
void initScreen1()
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

// O-----------------------------------------------------------------------O
// | SCREEN 2 - SETTINGS SCREEN
// O-----------------------------------------------------------------------O
void initScreen2()
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

// O-----------------------------------------------------------------------O
// | SCREEN 3 - JOIN GAME SCREEN
// O-----------------------------------------------------------------------O
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

// O-----------------------------------------------------------------------O
// | SCREEN 4 - HOST GAME SCREEN
// O-----------------------------------------------------------------------O
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

// O-----------------------------------------------------------------------O
// | SCREEN 5 - IN-GAME SCREEN (Playfield/Table)
// O-----------------------------------------------------------------------O
void initScreen5() // in-game screen
{
    screen5 = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen5, lv_color_hex(0x000000), LV_PART_MAIN);

    // O-----------------------------------------------------------------------O
    // | Header label
    // O-----------------------------------------------------------------------O
    lv_obj_t* sc5HeaderLabel = lv_label_create(screen5);
    lv_label_set_text(sc5HeaderLabel, " - In-Game Screen - ");
    lv_obj_set_style_text_color(screen5, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align(sc5HeaderLabel, LV_ALIGN_TOP_MID, 0, 0);

    // O-----------------------------------------------------------------------O
    // | Player 1 Health Label
    // O-----------------------------------------------------------------------O
    // Label
    lv_obj_t* sc5Player1HealthLabelLabel = lv_label_create(screen5);
    lv_label_set_text(sc5Player1HealthLabelLabel, "Player 1: ");
    lv_obj_set_pos(sc5Player1HealthLabelLabel, 20, 45);

    // Value
    sc5Player1HealthValueLabel = lv_label_create(screen5);
    lv_label_set_text(sc5Player1HealthValueLabel, "??");
    lv_obj_set_pos(sc5Player1HealthValueLabel, 100, 45);

    // O-----------------------------------------------------------------------O
    // | Player 2 Health Label
    // O-----------------------------------------------------------------------O
    // Label
    lv_obj_t* sc5Player2HealthLabelLabel = lv_label_create(screen5);       
    lv_label_set_text(sc5Player2HealthLabelLabel, "Player 2: ");
    lv_obj_set_pos(sc5Player2HealthLabelLabel, 180, 45);

    // Value
    sc5Player2HealthValueLabel = lv_label_create(screen5);
    lv_label_set_text(sc5Player2HealthValueLabel, "??");
    lv_obj_set_pos(sc5Player2HealthValueLabel, 260, 45);

    // O-----------------------------------------------------------------------O
    // | Player 3 Health Label
    // O-----------------------------------------------------------------------O
    // Label
    lv_obj_t* sc5Player3HealthLabelLabel = lv_label_create(screen5);
    lv_label_set_text(sc5Player3HealthLabelLabel, "Player 3: ");
    lv_obj_set_pos(sc5Player3HealthLabelLabel, 20, 85);

    // Value
    sc5Player3HealthValueLabel = lv_label_create(screen5);
    lv_label_set_text(sc5Player3HealthValueLabel, "??");
    lv_obj_set_pos(sc5Player3HealthValueLabel, 100, 85);

    // O-----------------------------------------------------------------------O
    // | Player 4 Health Label
    // O-----------------------------------------------------------------------O
    // Label
    lv_obj_t* sc5Player4HealthLabelLabel = lv_label_create(screen5);
    lv_label_set_text(sc5Player4HealthLabelLabel, "Player 4: ");
    lv_obj_set_pos(sc5Player4HealthLabelLabel, 180, 85);

    // Value
    sc5Player4HealthValueLabel = lv_label_create(screen5);
    lv_label_set_text(sc5Player4HealthValueLabel, "??");
    lv_obj_set_pos(sc5Player4HealthValueLabel, 260, 85);

    int btnXSize = 90;
    int btnYSize = 60;
    // O-----------------------------------------------------------------------O
    // | Decrease Health Value Button
    // O-----------------------------------------------------------------------O
    lv_obj_t* btnDecreaseHealthValue = lv_button_create(screen5);
    lv_obj_set_pos(btnDecreaseHealthValue, 10, 155);
    lv_obj_set_size(btnDecreaseHealthValue, btnXSize, btnYSize);
    lv_obj_add_event_cb(btnDecreaseHealthValue, btn_event_screen5DecreaseHealthButton_cb, LV_EVENT_ALL, NULL);

    lv_obj_t* btnDecreaseHealthLabel = lv_label_create(btnDecreaseHealthValue);
    lv_label_set_text(btnDecreaseHealthLabel, "V");
    lv_obj_center(btnDecreaseHealthLabel);

    // O-----------------------------------------------------------------------O
    // | Increase Health Value Button
    // O-----------------------------------------------------------------------O
    lv_obj_t* btnIncreaseHealthValue = lv_button_create(screen5);
    lv_obj_set_pos(btnIncreaseHealthValue, 220, 155);
    lv_obj_set_size(btnIncreaseHealthValue, btnXSize, btnYSize);
    lv_obj_add_event_cb(btnIncreaseHealthValue, btn_event_screen5IncreaseHealthButton_cb, LV_EVENT_ALL, NULL);

    lv_obj_t* btnIncreaseHealthLabel = lv_label_create(btnIncreaseHealthValue);
    lv_label_set_text(btnIncreaseHealthLabel, "/\\");
    lv_obj_center(btnIncreaseHealthLabel);
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
    lv_refr_now(NULL); // i guess not a good way?
    delaySafeMilli(5);
}

void loadScreen2()
{
    lv_screen_load(screen2);
    lv_refr_now(NULL);
    delaySafeMilli(5);
}

void loadScreen3()
{
    lv_screen_load(screen3);
    lv_refr_now(NULL);
    delaySafeMilli(5);
}

void loadScreen4()
{
    lv_screen_load(screen4);
    lv_refr_now(NULL);
    delaySafeMilli(5);
}

void loadScreen5()
{
    lv_screen_load(screen5);
    lv_refr_now(NULL);
    delaySafeMilli(5);
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


// O=======================================================================O
// |                                                                       |
// |                             **** SETUP ****                           |
// |                                                                       |
// O=======================================================================O
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
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, my_pointer_read);

    resetAllGameStateStructs();

    initScreens(); // create all screens
    loadScreen1(); // load home screen

    Serial.println( "Setup done, entering loop" );
}


// O=======================================================================O
// |                                                                       |
// |                       GAME STATES                                     |
// |                                                                       |
// O=======================================================================O

bool ssidStartsWithPrefix(const char* s)
{
    return s && strncmp(s, "box", 3) == 0;
}

void STATE_HOSTING_GAME()
{
    // reset all previous game data get ready for new game
    resetAllGameStateStructs();

    // TODO: Check if no game name and force them to name game
    const char* ssid = lv_textarea_get_text(taGameName);

    Serial.print("ssid from text area: "); Serial.println(ssid);
    // In order to filter out spurious SSIDs, I am appending
    // the prefix "box" to the user's chosen SSID. I will
    // then filter out all SSIDs found that do not start with "box"
    const char* prefix = "box";
    size_t len = strlen(prefix) + strlen(ssid) + 1;
    char* ssidWithPrefix = new char[len];
    strcpy(ssidWithPrefix, prefix);
    strcat(ssidWithPrefix, ssid);

    // TODO: set password in settings screen lv_textarea_get_text(taGamePass);
    const char* password = "";

    disconnectWifi();
    Serial.print("Starting wifi with SSID "); Serial.println(ssidWithPrefix);
    WiFi.softAP(ssidWithPrefix, password);

    udp.begin(UDP_PORT);

    STATE_JOINING_GAME_HOST_NEGOTIATE_WITH_GUESTS();
}

// FIND ALL VALID SSIDS AND DISPLAY THEM ON A LIST
// CLICKING AN SSID ON THE LIST WILL FIRE AN EVENT
// TO CONNECT TO THE SSID AND START THE GAME LOOP
void STATE_JOINING_GAME_LOOK_FOR_VALID_SSIDS()
{
    // reset all previous game data get ready for new game
    resetAllGameStateStructs();

    disconnectWifi();
    int n = WiFi.scanNetworks();
    int boxSsids = 0;
    for (int i = 0; i < n; i++)
    {
        if(ssidStartsWithPrefix(WiFi.SSID(i).c_str())) boxSsids++;
    }

    if(boxSsids == 0)
    {
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

    availableSSIDList = lv_list_create(lv_screen_active());
    lv_obj_set_pos(availableSSIDList, 10, 25);
    lv_obj_set_size(availableSSIDList, 300, 160);

    for(int i = 0; i < boxSsids; i++)
    {
        lv_obj_t *list_btn = lv_list_add_btn(availableSSIDList, LV_SYMBOL_RIGHT, SSIDList[i].c_str());
        lv_obj_add_event_cb(list_btn, ssid_listbox_event_handler, LV_EVENT_CLICKED, NULL);
    }
}

// SSID has been chosen, now connect to host and establish player ID 
void STATE_JOINING_GAME_CONNECT_TO_SSID_GUEST(const char* chosenSSID, const char* password)
{
    WiFi.begin(chosenSSID, password);
    int tryCount = 0;
    int numTries = 50;
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
        GSD_PlayerDataStruct.HostIPAddress = WiFi.gatewayIP();
        STATE_JOINING_GAME_GUEST_NEGOTIATE_WITH_HOST();
    }
}

const char* UDPMSG_DiscoveryMessage = "R2J"; // request to join
const char* UDPMSG_requestDisconnectMessage = "R2DC"; // request to disconnect
const char* UDPMSG_acknowledgementMessage = "OK";
const char* UDPMSG_gameStartingMessage = "GETREADY";
void STATE_JOINING_GAME_GUEST_NEGOTIATE_WITH_HOST()
{
    // guest does a udp.send "R2J"
    // host responds back with PID=(the playerGameIDOrder enumerated value)
    // guest acknowledges and waits for game start message
    Serial.println("Inside STATE_JOINING_GAME_GUEST_NEGOTIATE_WITH_HOST()");
    unsigned long startTime = millis();
    unsigned long timeOut = 60000;
    bool game_started = false;
    bool exit_loop = false;
    char rxBuffer[128];
    while(!game_started && !exit_loop)
    {
        Serial.println("Sending to HOST DISCOVERY MESSAGE ");
        udp.beginPacket(GSD_PlayerDataStruct.HostIPAddress, UDP_PORT);
        udp.write((const uint8_t*)UDPMSG_DiscoveryMessage, strlen(UDPMSG_DiscoveryMessage));
        udp.endPacket();

        // give host some time to respond
        bool hostTimeout = false;
        bool hostAcknowledge = false;
        unsigned long hostStartTime = millis();
        unsigned long hostTimeToWait = 2000;
        Serial.println("Waiting for HOST response to DISCOVERY MESSAGE");
        while(!hostTimeout && !hostAcknowledge)
        {
            int packetSize = udp.parsePacket();
            if(packetSize > 0)
            {
                int len = udp.read(rxBuffer, sizeof(rxBuffer) - 1);
                rxBuffer[len] = '\0';

                if(sizeof(rxBuffer) >= 4 && rxBuffer[0] == 'P' && rxBuffer[1] == 'I' && rxBuffer[2] == 'D' && rxBuffer[3] == '=')
                {
                    int playerID = atoi(rxBuffer + 4);

                    Serial.print("Decoded received PID as ");
                    Serial.println(playerID);

                    if(playerID >= 0 && playerID <= TOTAL_PLAYER_COUNT)
                    {
                        GSD_PlayerDataStruct.playerId = (playerGameIDOrder)playerID;
                        udp.beginPacket(GSD_PlayerDataStruct.HostIPAddress, UDP_PORT);
                        udp.write((const uint8_t*)UDPMSG_acknowledgementMessage, strlen(UDPMSG_acknowledgementMessage));
                        udp.endPacket();

                        // TODO: print some message on the screen about how we're now connected to the host
                        // and just waiting for the game to begin
                        Serial.println("Successfully joined game! Waiting for host to kick off game loop");

                        // give host some time to respond, right now game won't start until all players are
                        // connected and acknowledged
                        bool waitForGameStartTimeout = false;
                        bool gameSuccessfullyStarted = false;
                        unsigned long waitForGameStartStartTime = millis();
                        unsigned long waitForGameStartTimeToWait = 300000;
                        while(!waitForGameStartTimeout && !gameSuccessfullyStarted)
                        {
                            int packetSize = udp.parsePacket();
                            if(packetSize > 0)
                            {
                                int len = udp.read(rxBuffer, sizeof(rxBuffer) - 1);
                                rxBuffer[len] = '\0';

                                if(strcmp(rxBuffer, UDPMSG_gameStartingMessage) == 0)
                                {
                                    // WE DID IT!
                                    gameSuccessfullyStarted = true;
                                    hostAcknowledge = true;
                                    game_started = true;
                                }
                            }

                            if(millis() - waitForGameStartStartTime >= waitForGameStartTimeToWait)
                            {
                                waitForGameStartTimeout = true;
                                hostTimeout = true;
                            }    
                        }

                        if(waitForGameStartTimeout)
                        {
                            // TODO: Put some kind of game timeout message up on the screen
                            Serial.println("ERROR waitForGameStartTimeout = true");
                            exit_loop = true;
                        }
                    }   
                }
            }

            if(millis() - hostStartTime >= hostTimeToWait) hostTimeout = true;
        }

        if(millis() - startTime >= timeOut) exit_loop = true;

        lv_timer_handler(); /* let the GUI do its work */
        delaySafeMilli(5); /* let this time pass */
    }

    if(game_started)
    {
        //STATE_IN_GAME_GUEST();
        IN_GAME_GUEST = true;
        loadScreen5();
    }
    else if(exit_loop)
    {
        // something bad happened
    }
}

void STATE_JOINING_GAME_HOST_NEGOTIATE_WITH_GUESTS()
{
    Serial.println("Inside STATE_JOINING_GAME_HOST_NEGOTIATE_WITH_GUESTS()");

    GSD_HostGameStruct.Player1_ID = playerGameIDOrder::PLAYER_1;
    GSD_HostGameStruct.Player1_IP_Address = WiFi.localIP();
    GSD_HostGameStruct.Player1_Port = UDP_PORT;

    bool exit_loop = false;
    bool game_started = false;
    unsigned long startTime = millis();
    unsigned long timeOut = 120000;
    int numJoinedPlayers = 1; // host counts as 1 player
    char rxBuffer[128];
    Serial.println("Entering main while loop for HOST NEGOTIATING WITH GUESTS state");
    while(!game_started && !exit_loop)
    {
        int packetSize = udp.parsePacket();
        if(packetSize > 0)
        {            
            Serial.println("Packet receieved");
            IPAddress senderIP = udp.remoteIP();
            uint16_t senderPort = udp.remotePort();

            int len = udp.read(rxBuffer, sizeof(rxBuffer) - 1);
            rxBuffer[len] = '\0';
            // buffer is now effectively a string with a null terminator
            // negotiation packets will essentially be string-based

            if(strcmp(rxBuffer, UDPMSG_DiscoveryMessage) == 0)
            {
                Serial.println("RECEIVED A DISCOVERY MESSAGE");
                // a new guest is attempting to join
                int playerNum = numJoinedPlayers + 1;
                String responseMsg = "PID=";
                responseMsg += playerNum;

                // send the player back their ID and give them 2 seconds to ack
                Serial.print("SENDING PLAYER THEIR PID (");
                Serial.print(responseMsg.c_str());
                Serial.println(")");
                udp.beginPacket(senderIP, senderPort);
                udp.write((const uint8_t*)responseMsg.c_str(), strlen(responseMsg.c_str()));
                udp.endPacket();

                // now wait a bit for their response
                bool receivedAck = false; 
                bool newPlayerTimeout = false;
                unsigned long newPlayerTimeToWait = 2000;
                unsigned long newPlayerStartTime = millis();
                while(!receivedAck && !newPlayerTimeout)
                {
                    int packetSize = udp.parsePacket();
                    if(packetSize > 0)
                    {
                        int len1 = udp.read(rxBuffer, sizeof(rxBuffer) - 1);
                        rxBuffer[len1] = '\0';

                        if(strcmp(rxBuffer, UDPMSG_acknowledgementMessage) == 0)
                        {
                            Serial.println("Player acknowledged their PID!");
                            receivedAck = true;
                        }
                    }
                    if((millis() - newPlayerStartTime) >= newPlayerTimeToWait) newPlayerTimeout = true;
                    delaySafeMilli(50);
                }

                if(receivedAck)
                {
                    numJoinedPlayers++;
                    Serial.print("Received acknowledgement from player"); 
                    Serial.println(numJoinedPlayers);

                    // TODO: someday be a better coder and move this all to arrays so I can use loops
                    if(numJoinedPlayers == 2)
                    {
                        GSD_HostGameStruct.Player2_ID = (playerGameIDOrder)numJoinedPlayers;
                        GSD_HostGameStruct.Player2_IP_Address = senderIP;
                        GSD_HostGameStruct.Player2_Port = senderPort;
                    }
                    else if(numJoinedPlayers == 3)
                    {
                        GSD_HostGameStruct.Player3_ID = (playerGameIDOrder)numJoinedPlayers;
                        GSD_HostGameStruct.Player3_IP_Address = senderIP;
                        GSD_HostGameStruct.Player3_Port = senderPort;
                    }
                    else if(numJoinedPlayers == 4)
                    {
                        GSD_HostGameStruct.Player4_ID = (playerGameIDOrder)numJoinedPlayers;
                        GSD_HostGameStruct.Player4_IP_Address = senderIP;
                        GSD_HostGameStruct.Player4_Port = senderPort;
                    }
                }
                else if(newPlayerTimeout)
                {
                    // guest did not respond with ACK, maybe show an error message on the hosting screen
                    Serial.println("new player joining timed out");
                }
            }
            else if(strcmp(rxBuffer, UDPMSG_requestDisconnectMessage) == 0)
            {
                // a connected guest is requesting to disconnect
                // TODO: if it's neccessary
            }
        }

        if(millis() - startTime >= timeOut) exit_loop = true;

        if(numJoinedPlayers >= TOTAL_PLAYER_COUNT) game_started = true;

        lv_timer_handler(); /* let the GUI do its work */
        delaySafeMilli(5); /* let this time pass */
    }

    if(exit_loop) 
    {
        // bummer, handle the timeout (print error message on screen exit to home screen)
        return;
    }

    if(game_started)
    {
        // move to the HOST_GAME_LOOP
        // send a get ready packet to all the joined guests

        // TODO: someday be a better coder and move this all to arrays so I can use loops

        // tell player 2 to move to the game loop
        udp.beginPacket(GSD_HostGameStruct.Player2_IP_Address, GSD_HostGameStruct.Player2_Port);
        udp.write((const uint8_t*)UDPMSG_gameStartingMessage, strlen(UDPMSG_gameStartingMessage));
        udp.endPacket();

        delaySafeMilli(100);

        // tell player 3 to move to the game loop
        udp.beginPacket(GSD_HostGameStruct.Player3_IP_Address, GSD_HostGameStruct.Player3_Port);
        udp.write((const uint8_t*)UDPMSG_gameStartingMessage, strlen(UDPMSG_gameStartingMessage));
        udp.endPacket();

        delaySafeMilli(100);

        // tell player 4 to move to the game loop
        udp.beginPacket(GSD_HostGameStruct.Player4_IP_Address, GSD_HostGameStruct.Player4_Port);
        udp.write((const uint8_t*)UDPMSG_gameStartingMessage, strlen(UDPMSG_gameStartingMessage));
        udp.endPacket();

        delaySafeMilli(100);

        
        //STATE_IN_GAME_HOST();
        IN_GAME_HOST = true;
        loadScreen5();
    }

}

void STATE_IN_GAME_GUEST()
{
    if(increaseHealthButtonClicked)
    {
        // TODO: increment player health in GSD_PlayerDataStruct
        Serial.println("INCREASE HEALTH");
        increaseHealthButtonClicked = false;
    }

    if(decreaseHealthButtonClicked)
    {
        // TODO: decrement player health
        Serial.println("DECREASE HEALTH");
        decreaseHealthButtonClicked = false;
    }

    GameStateStruct gameState;

    Serial.println("I have made it into the STATE_IN_GAME_GUEST function");
    Serial.print("My player ID was ");
    Serial.println((int)GSD_PlayerDataStruct.playerId);

    // TODO: first, create the screen and assign different players to the different labels
    // start a while loop that does 3 things:
    // 1. receives the gamestate struct from the host and updates its own gamestate variable
    // 2. 1-4 times per second sends its health (and poison and stuff?) back to the host
    // 3. 

}

void STATE_IN_GAME_HOST()
{   
    if(increaseHealthButtonClicked)
    {
        // TODO: increment player 1 health
        Serial.println("INCREASE HEALTH");
        increaseHealthButtonClicked = false;
    }

    if(decreaseHealthButtonClicked)
    {
        // TODO: decrement player 1 health
        Serial.println("DECREASE HEALTH");
        decreaseHealthButtonClicked = false;
    }

/*
    Serial.println("I have made it into the STATE_IN_GAME_HOST function");
    Serial.print("Player 1 Info: ");
    Serial.print(GSD_HostGameStruct.Player1_ID);
    Serial.print(", ");
    Serial.print(GSD_HostGameStruct.Player1_IP_Address);
    Serial.print(", ");
    Serial.println(GSD_HostGameStruct.Player1_Port);

    Serial.print("Player 2 Info: ");
    Serial.print(GSD_HostGameStruct.Player2_ID);
    Serial.print(", ");
    Serial.print(GSD_HostGameStruct.Player2_IP_Address);
    Serial.print(", ");
    Serial.println(GSD_HostGameStruct.Player2_Port);

    Serial.print("Player 3 Info: ");
    Serial.print(GSD_HostGameStruct.Player3_ID);
    Serial.print(", ");
    Serial.print(GSD_HostGameStruct.Player3_IP_Address);
    Serial.print(", ");
    Serial.println(GSD_HostGameStruct.Player3_Port);

    Serial.print("Player 4 Info: ");
    Serial.print(GSD_HostGameStruct.Player4_ID);
    Serial.print(", ");
    Serial.print(GSD_HostGameStruct.Player4_IP_Address);
    Serial.print(", ");
    Serial.println(GSD_HostGameStruct.Player4_Port);
*/

}

void loop()
{
    if(IN_GAME_GUEST)
    {
        STATE_IN_GAME_GUEST();
    }
    else if(IN_GAME_HOST)
    {
        STATE_IN_GAME_HOST();
    }

    lv_timer_handler(); /* let the GUI do its work */
    delaySafeMilli(5); /* let this time pass */
}
