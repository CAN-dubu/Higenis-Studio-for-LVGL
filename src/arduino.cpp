// 아두이노 스케치: 화면 코드 + 보드 연결(화면·터치·SD) + 설정 파일
#include "app.h"
#include "image.h"

#include <algorithm>
#include <cstdio>
#include <set>

const BoardField kBoardFields[] = {
    {"driver", &Board::driver}, {"rotation", &Board::rotation}, {"mhz", &Board::mhz},
    {"sck", &Board::sck}, {"mosi", &Board::mosi}, {"miso", &Board::miso}, {"cs", &Board::cs}, {"dc", &Board::dc}, {"rst", &Board::rst}, {"bl", &Board::bl},
    {"invert", &Board::invert},
    {"touch", &Board::touch}, {"tcs", &Board::tcs}, {"tirq", &Board::tirq}, {"tsck", &Board::tsck}, {"tmosi", &Board::tmosi}, {"tmiso", &Board::tmiso},
    {"tx0", &Board::tx0}, {"tx1", &Board::tx1}, {"ty0", &Board::ty0}, {"ty1", &Board::ty1}, {"tswap", &Board::tswap},
    {"buf", &Board::buf}, {"dbl", &Board::dbl}, {"psram", &Board::psram}, {"period", &Board::period},
    {"sd", &Board::sd}, {"sdcs", &Board::sdcs}, {"sdsck", &Board::sdsck}, {"sdmosi", &Board::sdmosi}, {"sdmiso", &Board::sdmiso}, {"sdmhz", &Board::sdmhz}};
const int kBoardFieldCount = (int)(sizeof kBoardFields / sizeof kBoardFields[0]);

const char* boardDriverKo(int d) {
    static const char* k[] = {"TFT_eSPI", "Adafruit ILI9341", "Adafruit ST7789", "직접 작성"};
    return k[std::clamp(d, 0, 3)];
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

    const std::string gen = " Higenis Studio가 만든 파일입니다. 다시 내보내면 덮어씁니다.";
    std::string ino = "/*\n * " + name + ".ino — 이 파일은 처음 한 번만 만들어집니다. 마음껏 고치세요.\n *\n"
                      " * 라이브러리: lvgl 9.1 이상" + std::string(b.driver == 0 ? ", TFT_eSPI" : b.driver == 1 ? ", Adafruit ILI9341" : b.driver == 2 ? ", Adafruit ST7735 and ST7789" : "") + "\n */\n"
                      "#include <lvgl.h>\n#include \"higenis_port.h\"\n#include \"ui.h\"\n\nvoid setup()\n{\n    Serial.begin(115200);\n    higenis_begin();   /* 화면, 터치" + std::string(sd ? ", SD 카드" : "") +
                      " */\n    ui_init();\n}\n\nvoid loop()\n{\n    higenis_loop();\n    /* 여기에 할 일을 적으세요. 오래 멈추면(delay) 화면도 멈춥니다 */\n}\n";
    if (!cbs.empty()) {
        ino += "\n/* 주기(이벤트) 콜백: 필요한 것의 주석을 풀어 쓰세요. 안 만들면 빈 함수가 쓰입니다 */\n";
        for (auto& c : cbs) ino += "\n/*\nvoid " + c + "(lv_event_t * e)\n{\n    Serial.println(\"" + c + "\");\n}\n*/\n";
    }

    std::string ev = "/*" + gen + "\n * 직접 만들지 않은 콜백이 비어 있어도 링크되도록 합니다.\n */\n#include \"ui.h\"\n\n"
                     "#if defined(__GNUC__)\n#define HIGENIS_WEAK __attribute__((weak))\n#else\n#define HIGENIS_WEAK\n#endif\n";
    for (auto& c : cbs) ev += "\nHIGENIS_WEAK void " + c + "(lv_event_t * e)\n{\n    LV_UNUSED(e);\n}\n";

    std::string ph = "/*" + gen + " */\n#ifndef HIGENIS_PORT_H\n#define HIGENIS_PORT_H\n\n"
                     "void higenis_begin(void);   /* LVGL, 화면, 터치" + std::string(sd ? ", SD 카드" : "") + " 준비 */\nvoid higenis_loop(void);    /* loop()에서 계속 부른다 */\n" +
                     (sd ? "bool higenis_sd_ok(void);   /* SD 카드를 찾았는가 */\n" : "") +
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
    if (sd) c += "#include <SD.h>\n";
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
    const bool touch = b.touch == 1 || (b.touch == 2 && b.driver == 0);

    // ── SD 카드 ──
    if (sd) {
        c += "\n/* SD 카드를 LVGL의 'S:' 드라이브로 잇는다 */\nstatic bool sd_ready = false;\nbool higenis_sd_ok(void) { return sd_ready; }\n"
             "static void * fs_open(lv_fs_drv_t * drv, const char * path, lv_fs_mode_t mode)\n{\n    LV_UNUSED(drv);\n    if (!sd_ready || mode != LV_FS_MODE_RD) return NULL;\n    File f = SD.open(path);\n    if (!f) return NULL;\n    return new File(f);\n}\n"
             "static lv_fs_res_t fs_close(lv_fs_drv_t * drv, void * fp)\n{\n    LV_UNUSED(drv);\n    File * f = (File *)fp;\n    f->close();\n    delete f;\n    return LV_FS_RES_OK;\n}\n"
             "static lv_fs_res_t fs_read(lv_fs_drv_t * drv, void * fp, void * buf, uint32_t btr, uint32_t * br)\n{\n    LV_UNUSED(drv);\n    const int n = ((File *)fp)->read((uint8_t *)buf, btr);\n    *br = n < 0 ? 0 : (uint32_t)n;\n    return n < 0 ? LV_FS_RES_HW_ERR : LV_FS_RES_OK;\n}\n"
             "static lv_fs_res_t fs_seek(lv_fs_drv_t * drv, void * fp, uint32_t pos, lv_fs_whence_t whence)\n{\n    LV_UNUSED(drv);\n    File * f = (File *)fp;\n    uint32_t p = pos;\n    if (whence == LV_FS_SEEK_CUR) p += (uint32_t)f->position();\n    if (whence == LV_FS_SEEK_END) p += (uint32_t)f->size();\n    return f->seek(p) ? LV_FS_RES_OK : LV_FS_RES_HW_ERR;\n}\n"
             "static lv_fs_res_t fs_tell(lv_fs_drv_t * drv, void * fp, uint32_t * pos)\n{\n    LV_UNUSED(drv);\n    *pos = (uint32_t)((File *)fp)->position();\n    return LV_FS_RES_OK;\n}\n"
             "static void sd_begin(void)\n{\n#if defined(ESP32)\n";
        if (b.sdsck >= 0) c += "    SPI.begin(" + N(b.sdsck) + ", " + N(b.sdmiso) + ", " + N(b.sdmosi) + ", " + N(b.sdcs) + ");\n";
        c += "    sd_ready = SD.begin(" + N(b.sdcs) + ", SPI, " + N(b.sdmhz) + "000000);\n#else\n    sd_ready = SD.begin(" + N(b.sdcs) + ");\n#endif\n    if (!sd_ready) Serial.println(\"Higenis: SD 카드를 찾을 수 없습니다\");\n\n"
             "    static lv_fs_drv_t drv;\n    lv_fs_drv_init(&drv);\n    drv.letter = 'S';\n    drv.open_cb = fs_open;\n    drv.close_cb = fs_close;\n    drv.read_cb = fs_read;\n    drv.seek_cb = fs_seek;\n    drv.tell_cb = fs_tell;\n    lv_fs_drv_register(&drv);\n}\n";
    }

    c += "\nstatic uint32_t tick_cb(void) { return (uint32_t)millis(); }\n\nstatic void * take(size_t bytes)\n{\n#if defined(ESP32)\n";
    c += b.psram ? "    void * p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);\n    return p ? p : heap_caps_malloc(bytes, MALLOC_CAP_8BIT);\n"
                 : "    return heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);\n";
    c += "#else\n    return malloc(bytes);\n#endif\n}\n\nvoid higenis_begin(void)\n{\n    lv_init();\n    lv_tick_set_cb(tick_cb);\n    screen_begin();\n" + std::string(touch ? "    touch_begin();\n" : "") + (sd ? "    sd_begin();\n" : "") +
         "\n    lv_display_t * disp = lv_display_create(SCR_W, SCR_H);\n    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);\n    lv_display_set_flush_cb(disp, flush_cb);\n"
         "    /* 그리기 버퍼. 메모리가 모자라면 줄 수를 줄여 다시 잡는다 */\n    void * b1 = NULL;\n    void * b2 = NULL;\n    uint32_t bytes = 0;\n    for (uint32_t n = BUF_LINES; n > 0 && !b1; n /= 2) {\n        bytes = SCR_W * n * 2;\n        b1 = take(bytes);\n" +
         (b.dbl ? "        if (b1) b2 = take(bytes);   /* 두 번째는 없어도 된다 */\n" : "") +
         "    }\n    if (!b1) {\n        Serial.println(\"Higenis: 버퍼를 만들 메모리가 없습니다\");\n        return;\n    }\n    lv_display_set_buffers(disp, b1, b2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);\n";
    if (touch) c += "\n    lv_indev_t * indev = lv_indev_create();\n    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);\n    lv_indev_set_read_cb(indev, touch_cb);\n";
    c += "}\n\nvoid higenis_loop(void)\n{\n    uint32_t wait = lv_timer_handler();\n    if (wait > 5) wait = 5;\n    delay(wait);\n}\n";

    // ── LVGL 설정 ──
    std::string opt = "-DLV_CONF_SKIP\n-DLV_COLOR_DEPTH=16\n-DLV_USE_STDLIB_MALLOC=1\n-DLV_USE_STDLIB_STRING=1\n-DLV_USE_STDLIB_SPRINTF=1\n-DLV_DEF_REFR_PERIOD=" + N(b.period) + "\n";
    std::string conf = "/*" + gen + "\n * ESP32는 build_opt.h가 같은 일을 하므로 이 파일이 필요 없습니다.\n * 다른 보드(RP2040, STM32 …)는 이 파일을 Arduino/libraries 폴더(lvgl 폴더 옆)에 복사하세요.\n */\n#ifndef LV_CONF_H\n#define LV_CONF_H\n\n"
                       "#define LV_COLOR_DEPTH 16\n#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB\n#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB\n#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB\n#define LV_DEF_REFR_PERIOD " + N(b.period) + "\n\n";
    for (int f : fonts) {
        opt += "-DLV_FONT_MONTSERRAT_" + N(f) + "=1\n";
        conf += "#define LV_FONT_MONTSERRAT_" + N(f) + " 1\n";
    }
    conf += "\n#endif\n";

    std::string rd = "Higenis Studio 아두이노 스케치: " + name + "\r\n\r\n"
        "1. 라이브러리 매니저에서 lvgl 9.1 이상을 설치하세요. 8.x에서는 컴파일되지 않습니다.\r\n" +
        std::string(b.driver == 0 ? "2. TFT_eSPI를 설치하고 User_Setup.h에서 화면 칩과 핀을 정하세요.\r\n" : b.driver == 1 ? "2. Adafruit ILI9341을 설치하세요.\r\n" : b.driver == 2 ? "2. Adafruit ST7735 and ST7789를 설치하세요.\r\n" : "2. higenis_port.cpp의 flush_cb와 screen_begin을 채우세요(다시 내보내면 덮어쓰므로 따로 보관하세요).\r\n") +
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
    out.push_back({"build_opt.h", opt});
    out.push_back({"lv_conf.h", conf});
    out.insert(out.end(), files.begin(), files.end());
    out.push_back({"README.txt", rd});
    return out;
}
