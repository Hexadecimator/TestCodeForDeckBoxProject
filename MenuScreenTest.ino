#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "AsyncUDP.h"

void STATE_HOSTING_GAME();
void STATE_JOINING_GAME();
void STATE_IN_GAME();

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

#define DRAW_BUF_SIZE (TFT_HOR_RES * TFT_VER_RES / 10 * (LV_COLOR_DEPTH / 8))
uint8_t* draw_buf;

lv_obj_t* screen1; // home screen   [x]
lv_obj_t* screen2; // settings menu [x]
lv_obj_t* screen3; // join screen   [ ]
lv_obj_t* screen4; // host screen   [ ]
lv_obj_t* screen5; // game screen   [ ]

uint8_t baseMAC[6] = { 0, 0, 0, 0, 0, 0 };
AsyncUDP udp;

lv_obj_t* taGameName;

// O=======================================================================O
// |                                                                       |
// |                             USEFUL FUNCTIONS                          |
// |                                                                       |
// O=======================================================================O

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
        STATE_JOINING_GAME();
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

void STATE_HOSTING_GAME()
{
    const char* ssid = lv_textarea_get_text(taGameName);
    const char* password = "";

    WiFi.disconnect();

    WiFi.softAP(ssid, password);

    IPAddress ip = WiFi.softAPIP();
}

void STATE_JOINING_GAME()
{
    int n = WiFi.scanNetworks();

    const char* SSIDList[n];
    Serial.println("Printing all available WiFi SSIDs: ");
    for (int i = 0; i < n; i++)
    {
        SSIDList[i] = WiFi.SSID(i).c_str();

        Serial.println(SSIDList[i]);
        // drop down list LVGL example: https://forum.lvgl.io/t/dynamic-list-for-song-selection/11900
        // TODO somehow populate a list of hosted WiFi games
        // you can iterate through the active SSIDs using
        // WiFi.SSID(i)
        // then when you find the SSID you want to connect to, 
        // use WiFi.begin(SSID, password); // password will be "" for now
        // while(WiFi.status()) != WL_CONNECTED) { delay(100); }
    }


}

void STATE_IN_GAME()
{

}

void loop()
{
    lv_timer_handler(); /* let the GUI do its work */
    delay(5); /* let this time pass */
}
