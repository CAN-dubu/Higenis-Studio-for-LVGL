// 아두이노 스케치: 화면 코드 + 보드 연결(화면·터치·SD) + 설정 파일
#include "app.h"
#include "image.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>

const BoardField kBoardFields[] = {
    {"driver", &Board::driver}, {"rotation", &Board::rotation}, {"mhz", &Board::mhz},
    {"sck", &Board::sck}, {"mosi", &Board::mosi}, {"miso", &Board::miso}, {"cs", &Board::cs}, {"dc", &Board::dc}, {"rst", &Board::rst}, {"bl", &Board::bl},
    {"invert", &Board::invert},
    {"touch", &Board::touch}, {"tcs", &Board::tcs}, {"tirq", &Board::tirq}, {"tsck", &Board::tsck}, {"tmosi", &Board::tmosi}, {"tmiso", &Board::tmiso},
    {"tx0", &Board::tx0}, {"tx1", &Board::tx1}, {"ty0", &Board::ty0}, {"ty1", &Board::ty1}, {"tswap", &Board::tswap},
    {"buf", &Board::buf}, {"dbl", &Board::dbl}, {"psram", &Board::psram}, {"period", &Board::period},
    {"sd", &Board::sd}, {"sdcs", &Board::sdcs}, {"sdsck", &Board::sdsck}, {"sdmosi", &Board::sdmosi}, {"sdmiso", &Board::sdmiso}, {"sdmhz", &Board::sdmhz},
    {"tsda", &Board::tsda}, {"tscl", &Board::tscl}, {"trst", &Board::trst}, {"tint", &Board::tint},
    {"sdmmc", &Board::sdmmc}, {"sdclk", &Board::sdclk}, {"sdcmd", &Board::sdcmd}, {"sdd0", &Board::sdd0}, {"sdd1", &Board::sdd1}, {"sdd2", &Board::sdd2}, {"sdd3", &Board::sdd3}};
const int kBoardFieldCount = (int)(sizeof kBoardFields / sizeof kBoardFields[0]);

const char* boardDriverKo(int d) {
    static const char* k[] = {"TFT_eSPI", "Adafruit ILI9341", "Adafruit ST7789", "직접 작성", "JD9165 · MIPI-DSI (ESP32-P4)"};
    return k[std::clamp(d, 0, 4)];
}
const char* boardTouchKo(int t) {
    static const char* k[] = {"없음", "XPT2046 (핀 직접)", "TFT_eSPI 내장", "GT911 (I²C · 다섯 손가락)"};
    return k[std::clamp(t, 0, 3)];
}
// JC1060P470 (ESP32-P4, 7인치 1024×600, GT911, TF 카드): 제조사 회로도 기준
void boardPresetJC1060(Board& b) {
    const int keepBuf = b.buf, keepDbl = b.dbl;
    b = Board();
    b.driver = 4; b.rotation = 1; b.rst = 27; b.bl = 23;
    b.touch = 3; b.tsda = 7; b.tscl = 8; b.trst = 22; b.tint = 21;
    b.sd = 1; b.sdmmc = 1;
    b.buf = keepBuf; b.dbl = keepDbl ? keepDbl : 1;
    b.period = 16;
}
const char* boardBufKo(int b) {
    static const char* k[] = {"화면의 1/10", "화면의 1/4", "화면의 1/2", "화면 전체"};
    return k[std::clamp(b, 0, 3)];
}
int boardBufLines(const Document& d) {
    static const int div[] = {10, 4, 2, 1};
    return std::max(1, (d.resH + div[std::clamp(d.board.buf, 0, 3)] - 1) / div[std::clamp(d.board.buf, 0, 3)]);
}
std::string sketchName(const Document& d) {
    std::string n = sanitizeIdent(d.project);
    return n.empty() ? "higenis_ui" : n;
}
bool usesSd(const Document& d) {
    for (auto& s : d.screens)
        for (auto& w : s.widgets)
            if (w.type == WType::Image && image::onSd(w) && (image::find(d, w.text) || !w.text2.empty())) return true;
    return false;
}

static std::string N(int v) { return std::to_string(v); }

std::vector<GenFile> generateArduino(const Document& in, const std::string& rev) {
    Document d = in;
    d.depth = 16;
    const Board& b = d.board;
    const bool sd = b.sd || usesSd(d);
    const std::string name = sketchName(d);
    std::vector<GenFile> files = generateCode(d, rev);

    // ui.h 머리말에서 필요한 글꼴을 읽는다
    std::set<int> fonts;
    for (auto& f : files)
        if (f.name == "ui.h")
            for (size_t p = 0; (p = f.text.find("LV_FONT_MONTSERRAT_", p)) != std::string::npos; p += 10) fonts.insert(atoi(f.text.c_str() + p + 19));
    fonts.insert(14);
    // 구현하지 않은 콜백은 빈 함수로(직접 만들면 그쪽이 쓰인다)
    std::set<std::string> cbs;
    for (auto& s : d.screens)
        for (auto& w : s.widgets)
            for (auto& e : w.events) if (validIdent(e.cb)) cbs.insert(e.cb);

    const bool dsi = b.driver == 4;
    const bool gt = b.touch == 3;
    const bool mmc = sd && b.sdmmc;
    const std::string gen = " Higenis Studio가 만든 파일입니다. 다시 내보내면 덮어씁니다.";
    std::string ino = "/*\n * " + name + ".ino — 이 파일은 처음 한 번만 만들어집니다. 마음껏 고치세요.\n *\n"
                      " * 라이브러리: lvgl 9.1 이상" + std::string(b.driver == 0 ? ", TFT_eSPI" : b.driver == 1 ? ", Adafruit ILI9341" : b.driver == 2 ? ", Adafruit ST7735 and ST7789" : "") + "\n" +
                      (dsi ? " * 보드: ESP32 보드 패키지 3.1 이상, 도구 > 보드 > ESP32P4 Dev Module, PSRAM: Enabled (README.txt 참고)\n" : "") + " */\n"
                      "#include <lvgl.h>\n#include \"higenis_port.h\"\n#include \"ui.h\"\n\nvoid setup()\n{\n    Serial.begin(115200);\n    higenis_begin();   /* 화면, 터치" + std::string(sd ? ", SD 카드" : "") +
                      " */\n    ui_init();\n}\n\nvoid loop()\n{\n    higenis_loop();\n    /* 여기에 할 일을 적으세요. 오래 멈추면(delay) 화면도 멈춥니다 */\n}\n";
    if (!cbs.empty()) {
        ino += "\n/* 주기(이벤트) 콜백: 필요한 것의 주석을 풀어 쓰세요. 안 만들면 빈 함수가 쓰입니다 */\n";
        for (auto& c : cbs) ino += "\n/*\nvoid " + c + "(lv_event_t * e)\n{\n    Serial.println(\"" + c + "\");\n}\n*/\n";
    }

    std::string ev = "/*" + gen + "\n * 직접 만들지 않은 콜백이 비어 있어도 링크되도록 합니다.\n */\n#include \"ui.h\"\n\n"
                     "#if defined(__GNUC__)\n#define HIGENIS_WEAK __attribute__((weak))\n#else\n#define HIGENIS_WEAK\n#endif\n";
    for (auto& c : cbs) ev += "\nHIGENIS_WEAK void " + c + "(lv_event_t * e)\n{\n    LV_UNUSED(e);\n}\n";

    std::string ph = "/*" + gen + " */\n#ifndef HIGENIS_PORT_H\n#define HIGENIS_PORT_H\n\n#include <stdbool.h>\n#include <stdint.h>\n\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n"
                     "void higenis_begin(void);   /* LVGL, 화면, 터치" + std::string(sd ? ", SD 카드" : "") + " 준비 */\nvoid higenis_loop(void);    /* loop()에서 계속 부른다 */\n" +
                     (sd ? "bool higenis_sd_ok(void);   /* SD 카드를 찾았는가 */\n" : "") +
                     (dsi ? "void higenis_backlight(uint8_t level);   /* 화면 밝기 0(꺼짐) ~ 255 */\n" : "") +
                     (gt ? "\n/* 여러 손가락: LVGL 부품은 첫 손가락으로 움직이고, 나머지는 여기서 읽는다 (화면 좌표) */\n#define HIGENIS_TOUCH_MAX 5\nint  higenis_touch_count(void);\nbool higenis_touch_point(int i, int32_t * x, int32_t * y);\n" : "") +
                     "\n#ifdef __cplusplus\n}\n#endif\n" +
                     "\n/* 예전(VELLUM) 이름으로 쓴 .ino도 그대로 컴파일되게 */\n#define vellum_begin higenis_begin\n#define vellum_loop higenis_loop\n" +
                     (sd ? "#define vellum_sd_ok higenis_sd_ok\n" : "") + "\n#endif\n";

    const int nw = (b.rotation & 1) ? d.resH : d.resW, nh = (b.rotation & 1) ? d.resW : d.resH;
    const int lines = boardBufLines(d);
    const bool spi = b.driver == 1 || b.driver == 2;
    std::string c = "/*" + gen + "\n * 화면 " + N(d.resW) + "x" + N(d.resH) + " RGB565 · " + boardDriverKo(b.driver) + " · 버퍼 " + N(lines) + "줄" + (b.dbl ? " 두 장" : " 한 장") + "\n */\n";
    c += "#include <Arduino.h>\n#include <SPI.h>\n#include <lvgl.h>\n#include \"higenis_port.h\"\n";
    c += "\n#if LVGL_VERSION_MAJOR < 9\n#error \"LVGL 9 이상이 필요합니다 (라이브러리 매니저에서 lvgl을 올려 주세요)\"\n#endif\n\n";
    if (b.driver == 0) c += "#include <TFT_eSPI.h>   /* 칩과 핀은 TFT_eSPI 라이브러리의 User_Setup.h에서 정합니다 */\n";
    if (b.driver == 1) c += "#include <Adafruit_ILI9341.h>\n";
    if (b.driver == 2) c += "#include <Adafruit_ST7789.h>\n";
    if (dsi) c += "#if !defined(CONFIG_IDF_TARGET_ESP32P4)\n#error \"MIPI-DSI 화면은 ESP32-P4 보드에서만 됩니다 (도구 > 보드 > ESP32P4 Dev Module)\"\n#endif\n"
                  "#include <esp_lcd_mipi_dsi.h>\n#include <esp_lcd_panel_ops.h>\n#include <esp_lcd_panel_io.h>\n#include <esp_ldo_regulator.h>\n";
    if (gt) c += "#include <Wire.h>\n";
    if (mmc) c += "#include <SD_MMC.h>\n#define SDFS SD_MMC\n";
    else if (sd) c += "#include <SD.h>\n#define SDFS SD\n";
    c += "#if defined(ESP32)\n#include <esp_heap_caps.h>\n#endif\n\n";
    c += "#define SCR_W " + N(d.resW) + "\n#define SCR_H " + N(d.resH) + "\n#define BUF_LINES " + N(lines) + "\n\n";

    // ── 화면 ──
    if (b.driver == 0) {
        c += "static TFT_eSPI tft;\n\nstatic void flush_cb(lv_display_t * disp, const lv_area_t * a, uint8_t * px)\n{\n"
             "    const uint32_t w = lv_area_get_width(a), h = lv_area_get_height(a);\n"
             "    tft.startWrite();\n    tft.setAddrWindow(a->x1, a->y1, w, h);\n    tft.pushColors((uint16_t *)px, w * h, true);\n    tft.endWrite();\n    lv_display_flush_ready(disp);\n}\n"
             "static void screen_begin(void)\n{\n    tft.begin();\n    tft.setRotation(" + N(b.rotation) + ");\n" + (b.invert ? "    tft.invertDisplay(true);\n" : "") + "    tft.fillScreen(TFT_BLACK);\n}\n";
    } else if (spi) {
        c += "#if defined(ESP32) && defined(HSPI)\nstatic SPIClass lcd_spi(HSPI);\n#else\nstatic SPIClass & lcd_spi = SPI;\n#endif\n";
        if (b.driver == 1) c += "static Adafruit_ILI9341 tft(&lcd_spi, " + N(b.dc) + ", " + N(b.cs) + ", " + N(b.rst) + ");\n";
        else c += "static Adafruit_ST7789 tft(&lcd_spi, " + N(b.cs) + ", " + N(b.dc) + ", " + N(b.rst) + ");\n";
        c += "\nstatic void flush_cb(lv_display_t * disp, const lv_area_t * a, uint8_t * px)\n{\n"
             "    const uint32_t w = lv_area_get_width(a), h = lv_area_get_height(a);\n"
             "    tft.startWrite();\n    tft.setAddrWindow(a->x1, a->y1, w, h);\n    tft.writePixels((uint16_t *)px, w * h);\n    tft.endWrite();\n    lv_display_flush_ready(disp);\n}\n"
             "static void screen_begin(void)\n{\n#if defined(ESP32)\n    lcd_spi.begin(" + N(b.sck) + ", " + N(b.miso) + ", " + N(b.mosi) + ", -1);\n#endif\n";
        if (b.driver == 1) c += "    tft.begin(" + N(b.mhz) + "000000);\n";
        else c += "    tft.init(" + N(nw) + ", " + N(nh) + ");\n    tft.setSPISpeed(" + N(b.mhz) + "000000);\n";
        c += "    tft.setRotation(" + N(b.rotation) + ");\n" + (b.invert ? "    tft.invertDisplay(true);\n" : "") + "    tft.fillScreen(0);\n";
        if (b.bl >= 0) c += "    pinMode(" + N(b.bl) + ", OUTPUT);\n    digitalWrite(" + N(b.bl) + ", HIGH);   /* 백라이트 */\n";
        c += "}\n";
    } else if (dsi) {
        // 패널 초기화 값은 제조사(Guition) 예제의 JD9165 표를 그대로 쓴다. 나머지는 ESP-IDF의 공개 API만으로 새로 썼다.
        static const char* kInit =
            "    {0x30, 1, 0, {0x00}}, {0xF7, 4, 0, {0x49, 0x61, 0x02, 0x00}}, {0x30, 1, 0, {0x01}}, {0x04, 1, 0, {0x0C}},\n"
            "    {0x05, 1, 0, {0x00}}, {0x06, 1, 0, {0x00}}, {0x0B, 1, 0, {0x11}}, {0x17, 1, 0, {0x00}}, {0x20, 1, 0, {0x04}},\n"
            "    {0x1F, 1, 0, {0x05}}, {0x23, 1, 0, {0x00}}, {0x25, 1, 0, {0x19}}, {0x28, 1, 0, {0x18}}, {0x29, 1, 0, {0x04}},\n"
            "    {0x2A, 1, 0, {0x01}}, {0x2B, 1, 0, {0x04}}, {0x2C, 1, 0, {0x01}}, {0x30, 1, 0, {0x02}}, {0x01, 1, 0, {0x22}},\n"
            "    {0x03, 1, 0, {0x12}}, {0x04, 1, 0, {0x00}}, {0x05, 1, 0, {0x64}}, {0x0A, 1, 0, {0x08}},\n"
            "    {0x0B, 11, 0, {0x0A, 0x1A, 0x0B, 0x0D, 0x0D, 0x11, 0x10, 0x06, 0x08, 0x1F, 0x1D}},\n"
            "    {0x0C, 11, 0, {0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D}},\n"
            "    {0x0D, 11, 0, {0x16, 0x1B, 0x0B, 0x0D, 0x0D, 0x11, 0x10, 0x07, 0x09, 0x1E, 0x1C}},\n"
            "    {0x0E, 11, 0, {0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D}},\n"
            "    {0x0F, 11, 0, {0x16, 0x1B, 0x0D, 0x0B, 0x0D, 0x11, 0x10, 0x1C, 0x1E, 0x09, 0x07}},\n"
            "    {0x10, 11, 0, {0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D}},\n"
            "    {0x11, 11, 0, {0x0A, 0x1A, 0x0D, 0x0B, 0x0D, 0x11, 0x10, 0x1D, 0x1F, 0x08, 0x06}},\n"
            "    {0x12, 11, 0, {0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D}},\n"
            "    {0x14, 4, 0, {0x00, 0x00, 0x11, 0x11}}, {0x18, 1, 0, {0x99}}, {0x30, 1, 0, {0x06}},\n"
            "    {0x12, 14, 0, {0x36, 0x2C, 0x2E, 0x3C, 0x38, 0x35, 0x35, 0x32, 0x2E, 0x1D, 0x2B, 0x21, 0x16, 0x29}},\n"
            "    {0x13, 14, 0, {0x36, 0x2C, 0x2E, 0x3C, 0x38, 0x35, 0x35, 0x32, 0x2E, 0x1D, 0x2B, 0x21, 0x16, 0x29}},\n"
            "    {0x30, 1, 0, {0x0A}}, {0x02, 1, 0, {0x4F}}, {0x0B, 1, 0, {0x40}}, {0x12, 1, 0, {0x3E}}, {0x13, 1, 0, {0x78}},\n"
            "    {0x30, 1, 0, {0x0D}}, {0x0D, 1, 0, {0x04}}, {0x10, 1, 0, {0x0C}}, {0x11, 1, 0, {0x0C}}, {0x12, 1, 0, {0x0C}},\n"
            "    {0x13, 1, 0, {0x0C}}, {0x30, 1, 0, {0x00}},\n"
            "    {0x11, 1, 120, {0x00}},   /* 잠 깨기 */\n"
            "    {0x29, 1, 50, {0x00}},    /* 화면 켜기 */\n";
        static const char* kRot[] = {"LV_DISPLAY_ROTATION_90", "LV_DISPLAY_ROTATION_0", "LV_DISPLAY_ROTATION_270", "LV_DISPLAY_ROTATION_180"};
        c += "/* JD9165 7인치 1024x600 MIPI-DSI 2레인. LVGL이 내부 RAM에 그린 조각을 DMA2D가 PSRAM의 화면 버퍼로 옮긴다 */\n"
             "#define LCD_W 1024\n#define LCD_H 600\n#define LCD_ROTATION " + std::string(kRot[b.rotation & 3]) + "\n"
             "#define LCD_RST " + N(b.rst) + "\n#define LCD_BL " + N(b.bl) + "\n\n"
             "#define CHECK(x) do { esp_err_t e_ = (x); if (e_ != ESP_OK) { Serial.printf(\"Higenis: %s 실패 (%s)\\n\", #x, esp_err_to_name(e_)); return false; } } while (0)\n\n"
             "/* 패널 초기화 값: 보드 제조사 예제의 esp_lcd_jd9165.c (Copyright 2024 Espressif Systems, Apache-2.0)에서 옮김 */\n"
             "typedef struct { uint8_t cmd, len, wait; uint8_t data[14]; } lcd_cmd_t;\n"
             "static const lcd_cmd_t jd9165_init[] = {\n" + std::string(kInit) + "};\n\n"
             "static esp_lcd_panel_handle_t panel = NULL;\nstatic uint8_t * rot_buf = NULL;   /* 세로·뒤집힘일 때 돌린 픽셀 */\n\n"
             "static bool IRAM_ATTR copy_done(esp_lcd_panel_handle_t p, esp_lcd_dpi_panel_event_data_t * e, void * ctx)\n{\n"
             "    LV_UNUSED(p);\n    LV_UNUSED(e);\n    lv_display_flush_ready((lv_display_t *)ctx);   /* 버퍼를 다시 써도 된다 */\n    return false;\n}\n\n"
             "static void flush_cb(lv_display_t * disp, const lv_area_t * a, uint8_t * px)\n{\n"
             "    if (!panel) {\n        lv_display_flush_ready(disp);\n        return;\n    }\n"
             "    lv_area_t r = *a;\n    const lv_display_rotation_t rot = lv_display_get_rotation(disp);\n"
             "    if (rot != LV_DISPLAY_ROTATION_0 && rot_buf) {\n"
             "        const lv_color_format_t cf = lv_display_get_color_format(disp);\n"
             "        const int32_t w = lv_area_get_width(a), h = lv_area_get_height(a);\n"
             "        lv_display_rotate_area(disp, &r);\n"
             "        lv_draw_sw_rotate(px, rot_buf, w, h, lv_draw_buf_width_to_stride(w, cf), lv_draw_buf_width_to_stride(lv_area_get_width(&r), cf), rot, cf);\n"
             "        px = rot_buf;\n    }\n"
             "    /* 비동기: 옮기기가 끝나면 copy_done이 LVGL에 알린다 */\n"
             "    if (esp_lcd_panel_draw_bitmap(panel, r.x1, r.y1, r.x2 + 1, r.y2 + 1, px) != ESP_OK) lv_display_flush_ready(disp);\n}\n\n"
             "void higenis_backlight(uint8_t level)\n{\n#if LCD_BL >= 0\n    static bool on = false;\n    if (!on) on = ledcAttach(LCD_BL, 20000, 8);\n    if (on) ledcWrite(LCD_BL, level);\n    else digitalWrite(LCD_BL, level ? HIGH : LOW);\n#else\n    LV_UNUSED(level);\n#endif\n}\n\n"
             "static bool screen_start(void)\n{\n"
             "    /* MIPI 물리층 전원 2.5 V (내부 LDO 3번) */\n    static esp_ldo_channel_handle_t ldo = NULL;\n    esp_ldo_channel_config_t lc = {};\n    lc.chan_id = 3;\n    lc.voltage_mv = 2500;\n    CHECK(esp_ldo_acquire_channel(&lc, &ldo));\n\n"
             "    esp_lcd_dsi_bus_handle_t bus = NULL;\n    esp_lcd_dsi_bus_config_t bc = {};\n    bc.bus_id = 0;\n    bc.num_data_lanes = 2;\n    bc.phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT;\n    bc.lane_bit_rate_mbps = 550;\n    CHECK(esp_lcd_new_dsi_bus(&bc, &bus));\n\n"
             "    esp_lcd_panel_io_handle_t io = NULL;\n    esp_lcd_dbi_io_config_t ic = {};\n    ic.virtual_channel = 0;\n    ic.lcd_cmd_bits = 8;\n    ic.lcd_param_bits = 8;\n    CHECK(esp_lcd_new_panel_io_dbi(bus, &ic, &io));\n\n"
             "    esp_lcd_dpi_panel_config_t pc = {};\n    pc.virtual_channel = 0;\n    pc.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;\n    pc.dpi_clock_freq_mhz = 56;   /* 약 60 Hz */\n"
             "    pc.pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565;\n    pc.num_fbs = 1;\n"
             "    pc.video_timing.h_size = LCD_W;\n    pc.video_timing.v_size = LCD_H;\n    pc.video_timing.hsync_pulse_width = 40;\n    pc.video_timing.hsync_back_porch = 160;\n    pc.video_timing.hsync_front_porch = 160;\n"
             "    pc.video_timing.vsync_pulse_width = 10;\n    pc.video_timing.vsync_back_porch = 23;\n    pc.video_timing.vsync_front_porch = 12;\n"
             "    pc.flags.use_dma2d = 1;\n    CHECK(esp_lcd_new_panel_dpi(bus, &pc, &panel));\n\n"
             "#if LCD_RST >= 0\n    pinMode(LCD_RST, OUTPUT);\n    digitalWrite(LCD_RST, HIGH);\n    delay(5);\n    digitalWrite(LCD_RST, LOW);\n    delay(10);\n    digitalWrite(LCD_RST, HIGH);\n    delay(120);\n#endif\n"
             "    const uint8_t madctl = 0x00;\n    CHECK(esp_lcd_panel_io_tx_param(io, 0x36, &madctl, 1));\n"
             "    for (size_t i = 0; i < sizeof jd9165_init / sizeof jd9165_init[0]; i++) {\n"
             "        CHECK(esp_lcd_panel_io_tx_param(io, jd9165_init[i].cmd, jd9165_init[i].data, jd9165_init[i].len));\n"
             "        if (jd9165_init[i].wait) delay(jd9165_init[i].wait);\n    }\n"
             "    CHECK(esp_lcd_panel_init(panel));\n    return true;\n}\n\n"
             "static void screen_begin(void)\n{\n#if LCD_BL >= 0\n    pinMode(LCD_BL, OUTPUT);\n    digitalWrite(LCD_BL, LOW);\n#endif\n"
             "    if (!psramFound()) Serial.println(\"Higenis: PSRAM이 없습니다. 도구 > PSRAM을 Enabled로 하세요 (화면 버퍼 1.2 MB)\");\n"
             "    if (!screen_start()) {\n        Serial.println(\"Higenis: 화면을 켜지 못했습니다\");\n        panel = NULL;\n    }\n}\n\n"
             "/* 디스플레이를 만든 뒤: 방향과 '옮기기 끝' 알림 */\nstatic void screen_attach(lv_display_t * disp, uint32_t buf_bytes)\n{\n"
             "    lv_display_set_rotation(disp, LCD_ROTATION);\n"
             "    if (LCD_ROTATION != LV_DISPLAY_ROTATION_0) {\n"
             "        rot_buf = (uint8_t *)heap_caps_aligned_alloc(64, buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);\n"
             "        if (!rot_buf) rot_buf = (uint8_t *)heap_caps_aligned_alloc(64, buf_bytes, MALLOC_CAP_SPIRAM);\n    }\n"
             "    if (!panel) return;\n    esp_lcd_dpi_panel_event_callbacks_t cbs = {};\n    cbs.on_color_trans_done = copy_done;\n    esp_lcd_dpi_panel_register_event_callbacks(panel, &cbs, disp);\n}\n";
    } else {
        c += "/* 화면 드라이버를 직접 적는 자리 */\nstatic void flush_cb(lv_display_t * disp, const lv_area_t * a, uint8_t * px)\n{\n"
             "    /* TODO: (a->x1, a->y1)-(a->x2, a->y2)에 RGB565 픽셀 px를 보내세요 */\n    LV_UNUSED(a);\n    LV_UNUSED(px);\n    lv_display_flush_ready(disp);\n}\n"
             "static void screen_begin(void)\n{\n    /* TODO: 화면 켜기 */\n}\n";
    }

    // ── 터치 ──
    if (b.touch == 1) {
        c += "\n/* XPT2046 저항막 터치: 라이브러리 없이 핀으로 직접 읽는다 */\nstatic uint16_t xpt(uint8_t cmd)\n{\n    uint16_t v = 0;\n"
             "    for (int i = 7; i >= 0; i--) {\n        digitalWrite(" + N(b.tmosi) + ", (cmd >> i) & 1);\n        digitalWrite(" + N(b.tsck) + ", HIGH);\n        digitalWrite(" + N(b.tsck) + ", LOW);\n    }\n"
             "    for (int i = 0; i < 16; i++) {\n        digitalWrite(" + N(b.tsck) + ", HIGH);\n        v = (uint16_t)(v << 1 | digitalRead(" + N(b.tmiso) + "));\n        digitalWrite(" + N(b.tsck) + ", LOW);\n    }\n    return (v >> 3) & 0x0FFF;\n}\n"
             "static void touch_cb(lv_indev_t * indev, lv_indev_data_t * data)\n{\n    LV_UNUSED(indev);\n    static lv_point_t last;\n    data->point = last;\n    data->state = LV_INDEV_STATE_RELEASED;\n";
        if (b.tirq >= 0) c += "    if (digitalRead(" + N(b.tirq) + ") == HIGH) return;\n";
        c += "    digitalWrite(" + N(b.tcs) + ", LOW);\n    const uint16_t z = xpt(0xB1);\n    xpt(0xD1);\n    uint32_t x = 0, y = 0;\n    for (int i = 0; i < 4; i++) { x += xpt(0xD1); y += xpt(0x91); }\n    xpt(0x90);\n    digitalWrite(" + N(b.tcs) + ", HIGH);\n"
             "    if (z < 100) return;\n    x /= 4;\n    y /= 4;\n    /* 보정: Serial.printf(\"%lu %lu\\n\", x, y)로 네 귀의 값을 보고 Higenis Studio의 보정값을 고치세요 */\n";
        if (b.tswap) c += "    { uint32_t t = x; x = y; y = t; }\n";
        c += "    const int32_t nx = constrain(map((long)x, " + N(b.tx0) + ", " + N(b.tx1) + ", 0, " + N(nw - 1) + "), 0, " + N(nw - 1) + ");\n"
             "    const int32_t ny = constrain(map((long)y, " + N(b.ty0) + ", " + N(b.ty1) + ", 0, " + N(nh - 1) + "), 0, " + N(nh - 1) + ");\n";
        switch (b.rotation & 3) {
        case 0: c += "    last.x = nx;\n    last.y = ny;\n"; break;
        case 1: c += "    last.x = ny;\n    last.y = " + N(nw - 1) + " - nx;\n"; break;
        case 2: c += "    last.x = " + N(nw - 1) + " - nx;\n    last.y = " + N(nh - 1) + " - ny;\n"; break;
        default: c += "    last.x = " + N(nh - 1) + " - ny;\n    last.y = nx;\n"; break;
        }
        c += "    data->point = last;\n    data->state = LV_INDEV_STATE_PRESSED;\n}\n"
             "static void touch_begin(void)\n{\n    pinMode(" + N(b.tcs) + ", OUTPUT);\n    digitalWrite(" + N(b.tcs) + ", HIGH);\n    pinMode(" + N(b.tsck) + ", OUTPUT);\n    digitalWrite(" + N(b.tsck) + ", LOW);\n    pinMode(" + N(b.tmosi) + ", OUTPUT);\n    pinMode(" + N(b.tmiso) + ", INPUT);\n" +
             (b.tirq >= 0 ? "    pinMode(" + N(b.tirq) + ", INPUT);\n" : std::string()) + "}\n";
    } else if (b.touch == 2 && b.driver == 0) {
        c += "\nstatic void touch_cb(lv_indev_t * indev, lv_indev_data_t * data)\n{\n    LV_UNUSED(indev);\n    static lv_point_t last;\n#ifdef TOUCH_CS\n    uint16_t x, y;\n"
             "    if (tft.getTouch(&x, &y)) {\n        last.x = x;\n        last.y = y;\n        data->state = LV_INDEV_STATE_PRESSED;\n    }\n    else\n#endif\n        data->state = LV_INDEV_STATE_RELEASED;\n    data->point = last;\n}\nstatic void touch_begin(void) {}\n";
    }
    if (gt) {
        // 패널 좌표 → 화면(LVGL) 좌표. DSI는 LVGL이 돌리므로 그 반대로 옮긴다
        const int pw = dsi ? 1024 : nw, ph_ = dsi ? 600 : nh;
        std::string map;
        if (dsi) {
            switch (b.rotation & 3) {
            case 1: map = "    *ox = x;\n    *oy = y;\n"; break;
            case 3: map = "    *ox = " + N(pw - 1) + " - x;\n    *oy = " + N(ph_ - 1) + " - y;\n"; break;
            case 0: map = "    *ox = " + N(ph_ - 1) + " - y;\n    *oy = x;\n"; break;
            default: map = "    *ox = y;\n    *oy = " + N(pw - 1) + " - x;\n"; break;
            }
        } else {
            map = b.tswap ? "    *ox = y;\n    *oy = x;\n" : "    *ox = x;\n    *oy = y;\n";
        }
        c += "\n/* GT911 정전식 터치 (I²C). 다섯 손가락까지 읽는다 */\n#define TP_SDA " + N(b.tsda) + "\n#define TP_SCL " + N(b.tscl) + "\n#define TP_RST " + N(b.trst) + "\n#define TP_INT " + N(b.tint) + "\n\n"
             "static uint8_t tp_addr = 0;   /* 0x5D 또는 0x14, 시작할 때 찾는다 */\nstatic uint8_t tp_n = 0;\nstatic int32_t tp_x[HIGENIS_TOUCH_MAX], tp_y[HIGENIS_TOUCH_MAX];\n\n"
             "static bool tp_read(uint16_t reg, uint8_t * buf, uint8_t n)\n{\n    Wire.beginTransmission(tp_addr);\n    Wire.write((uint8_t)(reg >> 8));\n    Wire.write((uint8_t)reg);\n"
             "    if (Wire.endTransmission(false) != 0) return false;\n    if (Wire.requestFrom(tp_addr, n) != n) return false;\n    for (uint8_t i = 0; i < n; i++) buf[i] = (uint8_t)Wire.read();\n    return true;\n}\n"
             "static void tp_clear(void)\n{\n    Wire.beginTransmission(tp_addr);\n    Wire.write(0x81);\n    Wire.write(0x4E);\n    Wire.write(0x00);\n    Wire.endTransmission();\n}\n"
             "static void tp_map(int32_t x, int32_t y, int32_t * ox, int32_t * oy)\n{\n" + map + "}\n\n"
             "int higenis_touch_count(void) { return tp_n; }\n"
             "bool higenis_touch_point(int i, int32_t * x, int32_t * y)\n{\n    if (i < 0 || i >= tp_n) return false;\n    *x = tp_x[i];\n    *y = tp_y[i];\n    return true;\n}\n\n"
             "static void touch_cb(lv_indev_t * indev, lv_indev_data_t * data)\n{\n    LV_UNUSED(indev);\n    static lv_point_t last;\n    uint8_t st = 0;\n"
             "    /* 새 값이 있을 때만 바꾼다(0x80). 없으면 앞의 상태를 그대로 둔다 */\n"
             "    if (tp_addr && tp_read(0x814E, &st, 1) && (st & 0x80)) {\n        uint8_t n = st & 0x0F;\n        if (n > HIGENIS_TOUCH_MAX) n = HIGENIS_TOUCH_MAX;\n        uint8_t raw[8 * HIGENIS_TOUCH_MAX];\n"
             "        if (n && !tp_read(0x814F, raw, (uint8_t)(n * 8))) n = 0;\n"
             "        for (uint8_t i = 0; i < n; i++) {\n            const uint8_t * p = raw + i * 8;   /* 번호, x(2), y(2), 크기(2), 예비 */\n"
             "            tp_map(p[1] | p[2] << 8, p[3] | p[4] << 8, &tp_x[i], &tp_y[i]);\n        }\n        tp_n = n;\n        tp_clear();\n    }\n"
             "    if (tp_n) {\n        last.x = tp_x[0];\n        last.y = tp_y[0];\n        data->state = LV_INDEV_STATE_PRESSED;\n    }\n    else data->state = LV_INDEV_STATE_RELEASED;\n    data->point = last;\n}\n\n"
             "static void touch_begin(void)\n{\n#if TP_RST >= 0\n    /* 리셋하면서 INT를 낮춰 두면 주소가 0x5D로 정해진다 */\n#if TP_INT >= 0\n    pinMode(TP_INT, OUTPUT);\n    digitalWrite(TP_INT, LOW);\n#endif\n"
             "    pinMode(TP_RST, OUTPUT);\n    digitalWrite(TP_RST, LOW);\n    delay(10);\n    digitalWrite(TP_RST, HIGH);\n    delay(10);\n#if TP_INT >= 0\n    pinMode(TP_INT, INPUT);\n#endif\n    delay(50);\n#endif\n"
             "    Wire.begin(TP_SDA, TP_SCL, 400000);\n"
             "    /* 대답만 보고 잡지 않는다: 같은 I²C에 시계 칩(0x14) 같은 다른 칩이 있다. 제품 번호(0x8140)가 '9xx'인지 본다.\n"
             "       리셋 뒤 깨어나는 데 시간이 걸리므로 0.3초까지 다시 찾는다 */\n"
             "    for (int t = 0; t < 30 && !tp_addr; t++) {\n        for (uint8_t a : {0x5D, 0x14}) {\n            uint8_t id[3] = {0, 0, 0};\n            tp_addr = a;\n"
             "            if (tp_read(0x8140, id, 3) && id[0] == '9' && id[1] >= '0' && id[1] <= '9' && id[2] >= '0' && id[2] <= '9') break;\n"
             "            tp_addr = 0;\n        }\n        if (!tp_addr) delay(10);\n    }\n"
             "    if (tp_addr) Serial.printf(\"Higenis: GT911 터치 0x%02X\\n\", tp_addr);\n"
             "    else Serial.println(\"Higenis: GT911 터치를 찾을 수 없습니다 (SDA·SCL 핀 확인)\");\n}\n";
    }
    const bool touch = b.touch == 1 || (b.touch == 2 && b.driver == 0) || gt;

    // ── SD 카드 ──
    if (sd) {
        c += "\n/* SD 카드를 LVGL의 'S:' 드라이브로 잇는다 */\nstatic bool sd_ready = false;\nbool higenis_sd_ok(void) { return sd_ready; }\n"
             "static void * fs_open(lv_fs_drv_t * drv, const char * path, lv_fs_mode_t mode)\n{\n    LV_UNUSED(drv);\n    if (!sd_ready || mode != LV_FS_MODE_RD) return NULL;\n    File f = SDFS.open(path);\n    if (!f) return NULL;\n    return new File(f);\n}\n"
             "static lv_fs_res_t fs_close(lv_fs_drv_t * drv, void * fp)\n{\n    LV_UNUSED(drv);\n    File * f = (File *)fp;\n    f->close();\n    delete f;\n    return LV_FS_RES_OK;\n}\n"
             "static lv_fs_res_t fs_read(lv_fs_drv_t * drv, void * fp, void * buf, uint32_t btr, uint32_t * br)\n{\n    LV_UNUSED(drv);\n    const int n = ((File *)fp)->read((uint8_t *)buf, btr);\n    *br = n < 0 ? 0 : (uint32_t)n;\n    return n < 0 ? LV_FS_RES_HW_ERR : LV_FS_RES_OK;\n}\n"
             "static lv_fs_res_t fs_seek(lv_fs_drv_t * drv, void * fp, uint32_t pos, lv_fs_whence_t whence)\n{\n    LV_UNUSED(drv);\n    File * f = (File *)fp;\n    uint32_t p = pos;\n    if (whence == LV_FS_SEEK_CUR) p += (uint32_t)f->position();\n    if (whence == LV_FS_SEEK_END) p += (uint32_t)f->size();\n    return f->seek(p) ? LV_FS_RES_OK : LV_FS_RES_HW_ERR;\n}\n"
             "static lv_fs_res_t fs_tell(lv_fs_drv_t * drv, void * fp, uint32_t * pos)\n{\n    LV_UNUSED(drv);\n    *pos = (uint32_t)((File *)fp)->position();\n    return LV_FS_RES_OK;\n}\n"
             "static void sd_begin(void)\n{\n";
        if (mmc) {
            c += "    /* SDMMC 4비트. ESP32-P4는 카드 전원을 내부 LDO 4번에서 받는다 */\n    SD_MMC.setPins(" + N(b.sdclk) + ", " + N(b.sdcmd) + ", " + N(b.sdd0) + ", " + N(b.sdd1) + ", " + N(b.sdd2) + ", " + N(b.sdd3) + ");\n"
                 "#if defined(CONFIG_IDF_TARGET_ESP32P4)\n    SD_MMC.setPowerChannel(4);\n#endif\n    sd_ready = SD_MMC.begin(\"/sdcard\", false);\n";
        } else {
            c += "#if defined(ESP32)\n";
            if (b.sdsck >= 0) c += "    SPI.begin(" + N(b.sdsck) + ", " + N(b.sdmiso) + ", " + N(b.sdmosi) + ", " + N(b.sdcs) + ");\n";
            c += "    sd_ready = SD.begin(" + N(b.sdcs) + ", SPI, " + N(b.sdmhz) + "000000);\n#else\n    sd_ready = SD.begin(" + N(b.sdcs) + ");\n#endif\n";
        }
        c += "    if (!sd_ready) Serial.println(\"Higenis: SD 카드를 찾을 수 없습니다\");\n\n"
             "    static lv_fs_drv_t drv;\n    lv_fs_drv_init(&drv);\n    drv.letter = 'S';\n    drv.open_cb = fs_open;\n    drv.close_cb = fs_close;\n    drv.read_cb = fs_read;\n    drv.seek_cb = fs_seek;\n    drv.tell_cb = fs_tell;\n    lv_fs_drv_register(&drv);\n}\n";
    }

    c += "\nstatic uint32_t tick_cb(void) { return (uint32_t)millis(); }\n\nstatic void * take(size_t bytes)\n{\n#if defined(ESP32)\n";
    if (dsi)   // DMA2D가 읽으므로 DMA가 닿는 메모리에, 캐시 줄(64바이트)에 맞춰
        c += b.psram ? "    void * p = heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM);\n    return p ? p : heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);\n"
                     : "    void * p = heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);\n    return p ? p : heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM);\n";
    else
        c += b.psram ? "    void * p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);\n    return p ? p : heap_caps_malloc(bytes, MALLOC_CAP_8BIT);\n"
                     : "    return heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);\n";
    c += "#else\n    return malloc(bytes);\n#endif\n}\n\nvoid higenis_begin(void)\n{\n    lv_init();\n    lv_tick_set_cb(tick_cb);\n    screen_begin();\n" + std::string(touch ? "    touch_begin();\n" : "") + (sd ? "    sd_begin();\n" : "") +
         (dsi ? "\n    /* 패널은 가로 1024x600. 세로·뒤집힘은 LVGL이 돌린다 */\n    lv_display_t * disp = lv_display_create(LCD_W, LCD_H);\n"
              : "\n    lv_display_t * disp = lv_display_create(SCR_W, SCR_H);\n") +
         "    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);\n    lv_display_set_flush_cb(disp, flush_cb);\n"
         "    /* 그리기 버퍼. 메모리가 모자라면 줄 수를 줄여 다시 잡는다 */\n    void * b1 = NULL;\n    void * b2 = NULL;\n    uint32_t bytes = 0;\n    for (uint32_t n = BUF_LINES; n > 0 && !b1; n /= 2) {\n        bytes = SCR_W * n * 2;\n        b1 = take(bytes);\n" +
         (b.dbl ? "        if (b1) b2 = take(bytes);   /* 두 번째는 없어도 된다 */\n" : "") +
         "    }\n    if (!b1) {\n        Serial.println(\"Higenis: 버퍼를 만들 메모리가 없습니다\");\n        return;\n    }\n    lv_display_set_buffers(disp, b1, b2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);\n" +
         (dsi ? "    screen_attach(disp, bytes);\n" : "");
    if (touch) c += "\n    lv_indev_t * indev = lv_indev_create();\n    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);\n    lv_indev_set_read_cb(indev, touch_cb);\n";
    c += "}\n\nvoid higenis_loop(void)\n{\n    uint32_t wait = lv_timer_handler();\n";
    if (dsi) c += "    static bool lit = false;\n    if (!lit) {   /* 첫 화면을 그린 뒤에 백라이트를 켠다 */\n        lit = true;\n        higenis_backlight(255);\n    }\n";
    c += "    if (wait > 5) wait = 5;\n    delay(wait);\n}\n";

    // ── LVGL 설정 ──
    std::string opt = "-DLV_CONF_SKIP\n-DLV_COLOR_DEPTH=16\n-DLV_USE_STDLIB_MALLOC=1\n-DLV_USE_STDLIB_STRING=1\n-DLV_USE_STDLIB_SPRINTF=1\n-DLV_DEF_REFR_PERIOD=" + N(b.period) + "\n";
    std::string conf = "/*" + gen + "\n * ESP32는 build_opt.h가 같은 일을 하므로 이 파일이 필요 없습니다.\n * 다른 보드(RP2040, STM32 …)는 이 파일을 Arduino/libraries 폴더(lvgl 폴더 옆)에 복사하세요.\n */\n#ifndef LV_CONF_H\n#define LV_CONF_H\n\n"
                       "#define LV_COLOR_DEPTH 16\n#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB\n#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB\n#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB\n#define LV_DEF_REFR_PERIOD " + N(b.period) + "\n\n";
    if (dsi) {   // ESP32-P4: 두 코어가 나눠 그린다 (Espressif 예제와 같은 설정)
        opt += "-DLV_USE_OS=LV_OS_FREERTOS\n-DLV_DRAW_SW_DRAW_UNIT_CNT=2\n";
        conf += "#define LV_USE_OS LV_OS_FREERTOS\n#define LV_DRAW_SW_DRAW_UNIT_CNT 2\n\n";
    }
    for (int f : fonts) {
        opt += "-DLV_FONT_MONTSERRAT_" + N(f) + "=1\n";
        conf += "#define LV_FONT_MONTSERRAT_" + N(f) + " 1\n";
    }
    conf += "\n#endif\n";

    std::string rd = "Higenis Studio 아두이노 스케치: " + name + "\r\n\r\n"
        "1. 라이브러리 매니저에서 lvgl 9.1 이상을 설치하세요. 8.x에서는 컴파일되지 않습니다.\r\n" +
        std::string(b.driver == 0 ? "2. TFT_eSPI를 설치하고 User_Setup.h에서 화면 칩과 핀을 정하세요.\r\n" : b.driver == 1 ? "2. Adafruit ILI9341을 설치하세요.\r\n" : b.driver == 2 ? "2. Adafruit ST7735 and ST7789를 설치하세요.\r\n"
                    : dsi ? "2. 보드 매니저의 esp32(Espressif) 3.1 이상이 필요합니다. 도구 메뉴를 이렇게 맞추세요.\r\n"
                            "   보드: ESP32P4 Dev Module / PSRAM: Enabled / Flash Size: 16MB / Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS)\r\n"
                            "   Chip Variant: 시리얼 모니터의 시작 메시지가 'chip revision: v1.x'면 Before v3.00, v3.x면 v3.00 or newer\r\n"
                            "   추가 라이브러리는 필요 없습니다(화면·터치 드라이버가 higenis_port.cpp에 들어 있습니다).\r\n"
                    : "2. higenis_port.cpp의 flush_cb와 screen_begin을 채우세요(다시 내보내면 덮어쓰므로 따로 보관하세요).\r\n") +
        (gt ? "   터치: 여러 손가락은 higenis_touch_count()와 higenis_touch_point()로 읽습니다. LVGL 부품은 첫 손가락으로 움직입니다.\r\n" : "") +
        (dsi ? "   밝기: higenis_backlight(0~255).\r\n" : "") +
        "3. ESP32: 스케치 폴더의 build_opt.h가 LVGL 설정을 대신합니다. 다른 보드: lv_conf.h를 libraries 폴더에 복사하세요.\r\n"
        "   설정이 바뀌면 처음부터 다시 컴파일해야 합니다(IDE를 껐다 켜면 됩니다).\r\n"
        "4. 플래시가 모자라면 ESP32는 도구 > Partition Scheme > Huge APP을 고르세요.\r\n" +
        (sd ? "5. sd 폴더 안의 것을 SD 카드(FAT32) 맨 위에 그대로 복사하세요.\r\n" : std::string()) +
        "\r\n다시 내보내도 " + name + ".ino는 덮어쓰지 않습니다. 나머지 파일은 새로 씁니다.\r\n"
        "시트나 그림이 바뀌어 더는 쓰지 않는 Higenis Studio 파일(.c .h .cpp)은 휴지통으로 옮깁니다. 직접 만든 파일은 건드리지 않습니다.\r\n";

    std::vector<GenFile> out;
    out.push_back({name + ".ino", ino, false, true});
    out.push_back({"higenis_port.cpp", c});
    out.push_back({"higenis_port.h", ph});
    if (!cbs.empty()) out.push_back({"higenis_events.c", ev});
    if (dsi) {   // JD9165 초기화 값(Apache-2.0)과 함께 라이선스 전문을 내보낸다
        std::ifstream in(std::filesystem::path(platformAssetsDir()) / L"licenses" / L"Apache-2.0.txt", std::ios::binary);
        std::string lic((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (lic.empty()) lic = "Apache License 2.0: https://www.apache.org/licenses/LICENSE-2.0\n";
        out.push_back({"LICENSE-jd9165.txt",
                       "higenis_port.cpp의 JD9165 초기화 값 표는 esp_lcd_jd9165.c\n(Copyright 2024 Espressif Systems (Shanghai) CO LTD)에서 옮긴 것으로, 아래 라이선스를 따릅니다.\n\n" + lic});
    }
    out.push_back({"build_opt.h", opt});
    out.push_back({"lv_conf.h", conf});
    out.insert(out.end(), files.begin(), files.end());
    out.push_back({"README.txt", rd});
    return out;
}
