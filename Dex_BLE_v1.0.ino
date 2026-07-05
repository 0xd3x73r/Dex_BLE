/*
 * ============================================================================
 *  ESP32 BLE HID Central Hub  —  Web Portal  v1
 *  Board     : ESP32-S3 Dev Module
 *  Author    : dexter-universe@proton.me
 *
 * ============================================================================
 */


struct KV {
    const char*   name;
    unsigned char mod;   
    unsigned char kc;    
};


struct Config {
    char ble_name[32];   
    char ap_ssid[32];    
    char ap_pass[64];    
};


struct __attribute__((packed)) KeyReport {
    unsigned char mod;        
    unsigned char reserved;   
    unsigned char keys[6];    
};

// One connected BLE host
struct Host {
    bool           active;        
    unsigned short conn_id;       
    char           addr[18];      
    bool           subscribed;    
    unsigned long  connected_s;   
    char           label[24];     
};


struct ScriptJob {
    char script[2048];
};


struct Macro {
    const char* id;
    const char* label;
    const char* emoji;
    const char* script;   
};


struct BlockEntry {
    bool          active;
    char          addr[18];           
    unsigned long blocked_until_s;   
};


struct DetectJob {
    bool           pending;
    unsigned short conn_id;
    unsigned char  addr_type;   
    unsigned long  fire_at_ms;  
};




#define CONFIG_BT_NIMBLE_MAX_CONNECTIONS  9   
#define CONFIG_BT_NIMBLE_MAX_BONDS       12   
#define CONFIG_BT_NIMBLE_MAX_CCCDS       64   



#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <NimBLECharacteristic.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>


class DualSerial : public Print {
public:
    void begin(unsigned long baud) {
        Serial.begin(baud);
        Serial0.begin(baud);
    }
    size_t write(uint8_t c) override {
        Serial.write(c);
        Serial0.write(c);
        return 1;
    }
    size_t write(const uint8_t* buf, size_t size) override {
        Serial.write(buf, size);
        Serial0.write(buf, size);
        return size;
    }
    void flush() { Serial.flush(); Serial0.flush(); }
};

static DualSerial LOG;

#undef  Serial
#define Serial LOG






#define MAX_HOSTS    9    
#define MAX_BLOCKED  8    


#define MOD_LCTRL   0x01
#define MOD_LSHIFT  0x02
#define MOD_LALT    0x04
#define MOD_LGUI    0x08


static const uint8_t KB_MAP[] = {
    0x05,0x01, 0x09,0x06, 0xA1,0x01, 0x85,0x01,
    0x05,0x07, 0x19,0xE0, 0x29,0xE7, 0x15,0x00, 0x25,0x01,
    0x75,0x01, 0x95,0x08, 0x81,0x02,
    0x95,0x01, 0x75,0x08, 0x81,0x01,
    0x95,0x06, 0x75,0x08, 0x15,0x00, 0x25,0x65,
    0x05,0x07, 0x19,0x00, 0x29,0x65, 0x81,0x00, 0xC0
};


static const uint8_t ASCII_HID[] = {
    0x2C,0x1E,0x34,0x20,0x21,0x22,0x24,0x34,
    0x26,0x27,0x25,0x2E,0x36,0x2D,0x37,0x38,
    0x27,0x1E,0x1F,0x20,0x21,0x22,0x23,0x24,
    0x25,0x26,0x33,0x33,0x36,0x2E,0x37,0x38,
    0x1F,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,
    0x0B,0x0C,0x0D,0x0E,0x0F,0x10,0x11,0x12,
    0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1A,
    0x1B,0x1C,0x1D,0x2F,0x31,0x30,0x23,0x2D,
    0x35,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,
    0x0B,0x0C,0x0D,0x0E,0x0F,0x10,0x11,0x12,
    0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1A,
    0x1B,0x1C,0x1D,0x2F,0x31,0x30,0x35
};


static const KV KEY_TABLE[] = {
    {"ENTER",0,0x28}, {"RETURN",0,0x28}, {"TAB",0,0x2B},
    {"SPACE",0,0x2C}, {"BACKSPACE",0,0x2A}, {"DELETE",0,0x4C},
    {"ESCAPE",0,0x29}, {"ESC",0,0x29},
    {"UP",0,0x52}, {"DOWN",0,0x51}, {"LEFT",0,0x50}, {"RIGHT",0,0x4F},
    {"HOME",0,0x4A}, {"END",0,0x4D}, {"PAGEUP",0,0x4B}, {"PAGEDOWN",0,0x4E},
    {"INSERT",0,0x49}, {"PRINTSCREEN",0,0x46},
    {"F1",0,0x3A},  {"F2",0,0x3B},  {"F3",0,0x3C},  {"F4",0,0x3D},
    {"F5",0,0x3E},  {"F6",0,0x3F},  {"F7",0,0x40},  {"F8",0,0x41},
    {"F9",0,0x42},  {"F10",0,0x43}, {"F11",0,0x44}, {"F12",0,0x45},
    {"GUI",MOD_LGUI,0}, {"WINDOWS",MOD_LGUI,0}, {"CMD",MOD_LGUI,0},
    {"CTRL",MOD_LCTRL,0}, {"CONTROL",MOD_LCTRL,0},
    {"SHIFT",MOD_LSHIFT,0}, {"ALT",MOD_LALT,0},
    {nullptr,0,0}
};





static Preferences  prefs;
static Config       cfg;


static Host          hosts[MAX_HOSTS];
static int           target_conn_id = -1;   


static BlockEntry    blocklist[MAX_BLOCKED];
static unsigned long default_block_secs = 120;  


static NimBLEServer*         pServer = nullptr;
static NimBLEHIDDevice*      pHID    = nullptr;
static NimBLECharacteristic* pKbd    = nullptr;
static NimBLEAdvertising*    pAdv    = nullptr;


static volatile bool pending_adv_restart = false;
static volatile int  pending_kick_cid    = -1;   
static char          pending_kick_addr[18] = {0};


#define MAX_DETECT_QUEUE 9
static DetectJob detect_queue[MAX_DETECT_QUEUE];


static char          slog_buf[1024];   
static int           slog_len = 0;
static volatile bool script_running = false;
static volatile bool script_abort   = false;
static ScriptJob     g_job;            


static WebServer web(80);



static void cfg_load() {
    
    prefs.begin("hid_cfg", false);
    prefs.getString("ble_name", "Dex_BLE").toCharArray(cfg.ble_name, sizeof(cfg.ble_name));        //CHANGE_ME
    prefs.getString("ap_ssid",  "Dex_BLE").toCharArray(cfg.ap_ssid, sizeof(cfg.ap_ssid));          //CHANGE_ME
    prefs.getString("ap_pass",  "dex_ble_ducky").toCharArray(cfg.ap_pass,   sizeof(cfg.ap_pass));  //CHANGE_ME
    Serial.printf("[CFG] Loaded — ble='%s'  ap_ssid='%s'\n", cfg.ble_name, cfg.ap_ssid);
}

static void cfg_save() {
    prefs.putString("ble_name", cfg.ble_name);
    prefs.putString("ap_ssid",  cfg.ap_ssid);
    prefs.putString("ap_pass",  cfg.ap_pass);
    Serial.println("[CFG] Saved to NVS");
}



static String label_nvs_key(const char* addr) {
    String k = "lbl_";
    for (const char* p = addr; *p; p++) if (*p != ':') k += *p;
    if (k.length() > 15) k = k.substring(0, 15);
    return k;
}

static void label_load(const char* addr, char* out, size_t out_len) {
    String key = label_nvs_key(addr);
    prefs.getString(key.c_str(), "").toCharArray(out, out_len);
    
}

static void label_save(const char* addr, const char* label) {
    String key = label_nvs_key(addr);
    prefs.putString(key.c_str(), label);
    Serial.printf("[LABEL] Saved '%s' → MAC %s (nvs key=%s)\n", label, addr, key.c_str());
}



static Host* host_find(unsigned short cid) {
    for (int i = 0; i < MAX_HOSTS; i++)
        if (hosts[i].active && hosts[i].conn_id == cid) return &hosts[i];
    return nullptr;
}

static void host_add(unsigned short cid, const char* addr) {
    if (host_find(cid)) return;  
    for (int i = 0; i < MAX_HOSTS; i++) {
        if (!hosts[i].active) {
            hosts[i] = {true, cid, {}, false, millis()/1000, {}};
            strncpy(hosts[i].addr, addr, 17); hosts[i].addr[17] = '\0';
            label_load(addr, hosts[i].label, sizeof(hosts[i].label));
            Serial.printf("[HOSTS] +conn=%u  addr=%s  slot=%d  label='%s'\n",
                          cid, addr, i, hosts[i].label);
            return;
        }
    }
    Serial.println("[HOSTS] ERROR: registry full, connection cannot be tracked");
}

static void host_remove(unsigned short cid) {
    for (int i = 0; i < MAX_HOSTS; i++) {
        if (hosts[i].active && hosts[i].conn_id == cid) {
            Serial.printf("[HOSTS] -conn=%u  addr=%s\n", cid, hosts[i].addr);
            memset(&hosts[i], 0, sizeof(Host));
            if (target_conn_id == (int)cid) {
                target_conn_id = -1;
                Serial.println("[HOSTS] Target reset to ALL (targeted host disconnected)");
            }
            return;
        }
    }
}


static void host_set_subscribed(unsigned short cid) {
    Host* h = host_find(cid);
    if (!h) {
        Serial.printf("[HOSTS] sub=1 for unknown conn=%u — ignored\n", cid);
        return;
    }
    if (!h->subscribed) {
        h->subscribed = true;
        Serial.printf("[HOSTS] conn=%u input pipe READY\n", cid);
    }
}

static int  host_count()     { int n=0; for(int i=0;i<MAX_HOSTS;i++) if(hosts[i].active) n++; return n; }
static bool any_subscribed() { for(int i=0;i<MAX_HOSTS;i++) if(hosts[i].active&&hosts[i].subscribed) return true; return false; }


static bool target_ready() {
    if (target_conn_id == -1) return any_subscribed();
    Host* h = host_find((unsigned short)target_conn_id);
    return h && h->subscribed;
}




static void block_cleanup_expired() {
    unsigned long now = millis()/1000;
    for (int i = 0; i < MAX_BLOCKED; i++) {
        if (blocklist[i].active && now >= blocklist[i].blocked_until_s) {
            Serial.printf("[BLOCK] Expired — unblocked %s\n", blocklist[i].addr);
            blocklist[i].active = false;
        }
    }
}

static bool is_blocked(const char* addr) {
    block_cleanup_expired();
    for (int i = 0; i < MAX_BLOCKED; i++)
        if (blocklist[i].active && strcmp(blocklist[i].addr, addr) == 0) return true;
    return false;
}

static unsigned long block_remaining(const char* addr) {
    block_cleanup_expired();
    unsigned long now = millis()/1000;
    for (int i = 0; i < MAX_BLOCKED; i++)
        if (blocklist[i].active && strcmp(blocklist[i].addr, addr) == 0)
            return (blocklist[i].blocked_until_s > now) ? (blocklist[i].blocked_until_s - now) : 0;
    return 0;
}

static void block_add(const char* addr, unsigned long secs) {
    int slot = -1;
    
    for (int i = 0; i < MAX_BLOCKED; i++)
        if (blocklist[i].active && strcmp(blocklist[i].addr, addr) == 0) { slot=i; break; }
    
    if (slot < 0)
        for (int i = 0; i < MAX_BLOCKED; i++)
            if (!blocklist[i].active) { slot=i; break; }
    
    if (slot < 0) {
        unsigned long soonest = (unsigned long)-1;
        for (int i = 0; i < MAX_BLOCKED; i++)
            if (blocklist[i].blocked_until_s < soonest) { soonest=blocklist[i].blocked_until_s; slot=i; }
    }
    blocklist[slot].active = true;
    strncpy(blocklist[slot].addr, addr, 17); blocklist[slot].addr[17] = '\0';
    blocklist[slot].blocked_until_s = millis()/1000 + secs;
    Serial.printf("[BLOCK] %s blocked for %lus (slot %d)\n", addr, secs, slot);
}

static void block_remove(const char* addr) {
    for (int i = 0; i < MAX_BLOCKED; i++) {
        if (blocklist[i].active && strcmp(blocklist[i].addr, addr) == 0) {
            blocklist[i].active = false;
            Serial.printf("[BLOCK] Manually unblocked %s\n", addr);
        }
    }
}




static bool         needsShift(char c)  { return (c>='A'&&c<='Z') || strchr("!@#$%^&*()_+{}|:\"<>?~",c); }
static unsigned char charToKc(char c)   { return (c<0x20||c>0x7E) ? 0 : ASCII_HID[c-0x20]; }


static KV lookupKey(const String& tok) {
    String t = tok; t.trim(); t.toUpperCase();
    for (int i = 0; KEY_TABLE[i].name; i++)
        if (t == KEY_TABLE[i].name) return KEY_TABLE[i];
    if (t.length() == 1) {
        char c = t[0], lc = (c>='A'&&c<='Z') ? (char)(c+32) : c;
        KV r; r.name=nullptr; r.mod=0; r.kc=charToKc(lc); return r;
    }
    KV r; r.name=nullptr; r.mod=0; r.kc=0; return r;
}


static void notifyTarget() {
    if (!pKbd) return;
    if (target_conn_id == -1)
        pKbd->notify();
    else
        pKbd->notify((unsigned short)target_conn_id);
}


static void sendKey(unsigned char mod, unsigned char kc, unsigned long hold_ms = 80) {
    if (!pKbd) return;
    KeyReport r={0,0,{0,0,0,0,0,0}}, rel={0,0,{0,0,0,0,0,0}};

    if (mod && kc) {
        
        r.mod=mod; r.keys[0]=0;
        pKbd->setValue((uint8_t*)&r,sizeof(r)); notifyTarget(); delay(40);
        
        r.mod=mod; r.keys[0]=kc;
        pKbd->setValue((uint8_t*)&r,sizeof(r)); notifyTarget(); delay(hold_ms);
        
        r.mod=mod; r.keys[0]=0;
        pKbd->setValue((uint8_t*)&r,sizeof(r)); notifyTarget(); delay(40);
    } else if (mod) {
        r.mod = mod;
        pKbd->setValue((uint8_t*)&r,sizeof(r)); notifyTarget(); delay(hold_ms);
    } else {
        r.keys[0] = kc;
        pKbd->setValue((uint8_t*)&r,sizeof(r)); notifyTarget(); delay(hold_ms);
    }
    
    pKbd->setValue((uint8_t*)&rel,sizeof(rel)); notifyTarget();
    delay(80);  
}

static void typeChar(char c) {
    unsigned char kc = charToKc(c);
    if (!kc) return;
    sendKey(needsShift(c) ? MOD_LSHIFT : 0, kc, 40);
}

static void typeString(const char* s) {
    while (*s && !script_abort) typeChar(*s++);
}





static void slog(const char* msg) {
    Serial.println(msg);
    int len = strlen(msg);
    if (slog_len + len + 2 >= (int)sizeof(slog_buf) - 1) {
        int keep = slog_len / 2;
        memmove(slog_buf, slog_buf + keep, slog_len - keep);
        slog_len -= keep;
    }
    memcpy(slog_buf + slog_len, msg, len);
    slog_len += len;
    slog_buf[slog_len++] = '\n';
    slog_buf[slog_len]   = '\0';
}


static void execLine(const String& line) {
    String l = line; l.trim();
    if (l.isEmpty() || l.startsWith("REM") || l.startsWith("//")) return;

    int    sp   = l.indexOf(' ');
    String verb = (sp >= 0) ? l.substring(0, sp) : l;
    String rest = (sp >= 0) ? l.substring(sp + 1) : "";
    verb.toUpperCase();

    char buf[160];

    if (verb == "STRING") {
        snprintf(buf, sizeof(buf), "  TYPE: \"%s\"", rest.c_str());
        slog(buf);
        typeString(rest.c_str());
        return;
    }
    if (verb == "DELAY") {
        int ms = rest.toInt();
        snprintf(buf, sizeof(buf), "  DELAY %d ms", ms);
        slog(buf);
        delay(ms);
        return;
    }
    if (verb == "DEFAULT_DELAY" || verb == "DEFAULTDELAY") return;

    
    unsigned char mod_acc = 0, final_kc = 0;
    String rem = l;
    while (rem.length() > 0) {
        rem.trim();
        int   s2  = rem.indexOf(' ');
        String tok = (s2 >= 0) ? rem.substring(0, s2) : rem;
        rem = (s2 >= 0) ? rem.substring(s2 + 1) : "";
        tok.trim();
        if (tok.isEmpty()) continue;
        KV kv = lookupKey(tok);
        if (kv.mod) mod_acc |= kv.mod;
        else if (kv.kc) final_kc = kv.kc;
    }
    if (mod_acc || final_kc) {
        snprintf(buf, sizeof(buf), "  KEY mod=0x%02X kc=0x%02X ← %s", mod_acc, final_kc, l.c_str());
        slog(buf);
        sendKey(mod_acc, final_kc, 100);
    } else {
        snprintf(buf, sizeof(buf), "  (no match) ← %s", l.c_str());
        slog(buf);
    }
}


static void scriptTask(void* pv) {
    ScriptJob* job = (ScriptJob*)pv;
    script_abort = false;

    
    slog_buf[0] = '\0'; slog_len = 0;
    slog("--- Script started ---");
    Serial.printf("[SCRIPT] Task running for target=%d\n", target_conn_id);

    String src = String(job->script);
    int    pos  = 0, total = src.length(), def_delay = 0;
    String last_line = "";

    while (pos <= total && !script_abort) {
        int    nl   = src.indexOf('\n', pos); if (nl < 0) nl = total;
        String line = src.substring(pos, nl); line.trim(); pos = nl + 1;
        if (line.isEmpty()) continue;

        String verb = line;
        int sp = verb.indexOf(' ');
        if (sp >= 0) verb = verb.substring(0, sp);
        verb.toUpperCase();

        char buf[160];

        if (verb == "REPEAT") {
            int n = line.substring(sp + 1).toInt();
            Serial.printf("[SCRIPT] REPEAT %d × '%s'\n", n, last_line.c_str());
            for (int i = 0; i < n && !script_abort; i++) execLine(last_line);

        } else if (verb == "DEFAULT_DELAY" || verb == "DEFAULTDELAY") {
            def_delay = line.substring(sp + 1).toInt();
            snprintf(buf, sizeof(buf), "  DEFAULT_DELAY=%d ms", def_delay);
            slog(buf);

        } else {
            snprintf(buf, sizeof(buf), "EXEC: %s", line.c_str());
            slog(buf);
            execLine(line);
            if (verb != "REM" && !line.startsWith("//")) last_line = line;
        }

        if (def_delay > 0 && verb != "DELAY") delay(def_delay);
    }

    const char* result = script_abort ? "--- ABORTED ---" : "--- Script finished OK ---";
    slog(result);
    Serial.printf("[SCRIPT] Done — %s\n", result);
    script_running = false;
    vTaskDelete(nullptr);
}

static bool runScript(const char* text) {
    if (script_running) {
        Serial.println("[SCRIPT] Rejected — already running");
        return false;
    }
    strncpy(g_job.script, text, sizeof(g_job.script) - 1);
    g_job.script[sizeof(g_job.script) - 1] = '\0';
    script_running = true;
    xTaskCreate(scriptTask, "script", 8192, &g_job, 2, nullptr);
    Serial.printf("[SCRIPT] Started (%d chars)\n", (int)strlen(text));
    return true;
}




static void os_detect_and_label(unsigned short cid, uint8_t addr_type) {
    Host* h = host_find(cid);
    if (!h) {
        Serial.printf("[OSDET] conn=%u no longer active — skipping\n", cid);
        return;
    }

    
    if (strlen(h->label) > 0) {
        Serial.printf("[OSDET] conn=%u already labelled '%s' — skipping\n",
                      cid, h->label);
        return;
    }

    
    uint16_t mtu = pServer ? pServer->getPeerMTU(cid) : 0;
    Serial.printf("[OSDET] conn=%u  addr_type=%u  mtu=%u\n", cid, addr_type, mtu);

    
    if (addr_type == BLE_ADDR_RANDOM && mtu <= 185) {
        bool already_requeued = false;
        for (int i = 0; i < MAX_DETECT_QUEUE; i++)
            if (detect_queue[i].pending && detect_queue[i].conn_id == cid)
                { already_requeued = true; break; }
        if (!already_requeued) {
            Serial.printf("[OSDET] conn=%u mtu=%u still low — retry in 5s\n", cid, mtu);
            for (int i = 0; i < MAX_DETECT_QUEUE; i++) {
                if (!detect_queue[i].pending) {
                    detect_queue[i] = {true, cid, addr_type, millis() + 5000};
                    break;
                }
            }
            return;  
        }
        Serial.printf("[OSDET] conn=%u retry: mtu still %u — labelling as-is\n", cid, mtu);
    }

    const char* detected = nullptr;

    if (addr_type == BLE_ADDR_RANDOM) {
        
        if      (mtu >= 500) detected = "🤖 Android";  // 🤖
        else if (mtu >= 200) detected = "🍎 Mac";       // 🍎
        else                 detected = "📱 iOS";        // 📱
    } else {
        // Public/static address: Windows, older Android, Linux
        if      (mtu >= 500)            detected = "🤖 Android";    // 🤖
        else if (mtu >= 200)            detected = "🪟 Windows PC";  // 🪟
        else if (mtu > 23 && mtu < 100) detected = "🐧 Linux";      // 🐧
        else                            detected = "🪟 Windows PC";  // 🪟
    }

    if (detected) {
        strncpy(h->label, detected, sizeof(h->label) - 1);
        h->label[sizeof(h->label) - 1] = '\0';
        label_save(h->addr, h->label);
        Serial.printf("[OSDET] conn=%u label set: '%s' (addr_type=%u mtu=%u)\n",
                      cid, h->label, addr_type, mtu);
    }
}





class CharCbs : public NimBLECharacteristicCallbacks {

    void onSubscribe(NimBLECharacteristic* c, NimBLEConnInfo& info, uint16_t sub) override {
        uint16_t cid = info.getConnHandle();
        Serial.printf("[BLE][t=%lu] CCCD conn=%u sub=%u uuid=%s\n",
                      millis(), cid, sub, c->getUUID().toString().c_str());

        if (sub > 0) {
            host_set_subscribed(cid);

            
            uint8_t atype = info.getAddress().getType();
            
            uint16_t mtu_now = pServer ? pServer->getPeerMTU(cid) : 0;
            Serial.printf("[OSDET] conn=%u sub=1 now: addr_type=%u mtu_now=%u (will re-read at +1500ms)\n",
                          cid, atype, mtu_now);
            bool queued = false;
            for (int i = 0; i < MAX_DETECT_QUEUE; i++) {
                if (!detect_queue[i].pending) {
                    detect_queue[i] = {true, cid, atype, millis() + 1500};
                    queued = true;
                    break;
                }
            }
            if (!queued) Serial.println("[OSDET] WARNING: detect queue full");

        } else {
            
            Serial.printf("[BLE] conn=%u sub=0 — ignoring (Windows reconnect pattern)\n", cid);
        }
    }

    void onRead(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
        
    }
};
static CharCbs charCbs;


class SrvCbs : public NimBLEServerCallbacks {

    void onConnect(NimBLEServer* srv, NimBLEConnInfo& info) override {
        String addr = info.getAddress().toString().c_str(); addr.toUpperCase();
        unsigned short cid = info.getConnHandle();

        
        if (is_blocked(addr.c_str())) {
            unsigned long rem = block_remaining(addr.c_str());
            Serial.printf("[BLE][t=%lu] Blocked conn=%u addr=%s — %lu s remaining\n",
                          millis(), cid, addr.c_str(), rem);
            pending_kick_cid = (int)cid;
            strncpy(pending_kick_addr, addr.c_str(), 17); pending_kick_addr[17] = '\0';
            return;
        }

        Serial.printf("[BLE][t=%lu] CONNECTED conn=%u  addr=%s  hosts=%d/%d  heap=%u\n",
                      millis(), cid, addr.c_str(), host_count()+1, MAX_HOSTS, ESP.getFreeHeap());
        host_add(cid, addr.c_str());

        
        if (host_count() < MAX_HOSTS) pending_adv_restart = true;
    }

    void onDisconnect(NimBLEServer*, NimBLEConnInfo& info, int reason) override {
        unsigned short cid  = info.getConnHandle();
        Host*  h            = host_find(cid);
        unsigned long now_s = millis()/1000;
        unsigned long age_s = h ? (now_s > h->connected_s ? now_s - h->connected_s : 0) : 0;

        Serial.printf("[BLE][t=%lu] DISCONNECTED conn=%u  reason=0x%02X(%d)  age=%lus  heap=%u\n",
                      millis(), cid, reason, reason, age_s, ESP.getFreeHeap());
        host_remove(cid);

        
        Serial.printf("[BLE] Hosts remaining: %d/%d\n", host_count(), MAX_HOSTS);
        for (int i = 0; i < MAX_HOSTS; i++)
            if (hosts[i].active)
                Serial.printf("[HOSTS]   conn=%u  addr=%s  sub=%d  label='%s'\n",
                              hosts[i].conn_id, hosts[i].addr,
                              (int)hosts[i].subscribed, hosts[i].label);

        
        pending_adv_restart = true;
    }

    void onAuthenticationComplete(NimBLEConnInfo& info) override {
        unsigned short cid = info.getConnHandle();
        Serial.printf("[BLE][t=%lu] Auth conn=%u  bonded=%d  encrypted=%d  bonds_total=%d\n",
                      millis(), cid, info.isBonded(), info.isEncrypted(),
                      NimBLEDevice::getNumBonds());
        
        String addr = info.getAddress().toString().c_str(); addr.toUpperCase();
        host_add(cid, addr.c_str());
        
    }
};




static void process_deferred_ble_actions() {
    
    if (pending_kick_cid >= 0) {
        Serial.printf("[BLE] Deferred-disconnect blocked conn=%u  addr=%s\n",
                      pending_kick_cid, pending_kick_addr);
        if (pServer) pServer->disconnect((unsigned short)pending_kick_cid);
        pending_kick_cid = -1;
    }

    
    if (pending_adv_restart) {
        pending_adv_restart = false;
        if (pAdv && !pAdv->isAdvertising()) {
            pAdv->start();
            Serial.printf("[BLE][t=%lu] Advertising restarted  hosts=%d\n",
                          millis(), host_count());
        }
    }

    
    unsigned long now = millis();
    for (int i = 0; i < MAX_DETECT_QUEUE; i++) {
        if (detect_queue[i].pending && now >= detect_queue[i].fire_at_ms) {
            detect_queue[i].pending = false;
            os_detect_and_label(detect_queue[i].conn_id, detect_queue[i].addr_type);
        }
    }
}


static void advertising_watchdog() {
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck < 2000) return;
    lastCheck = millis();

    if (pAdv && !pAdv->isAdvertising() && host_count() < MAX_HOSTS) {
        pAdv->start();
        Serial.printf("[BLE][t=%lu] Watchdog restarted advertising  hosts=%d/%d\n",
                      millis(), host_count(), MAX_HOSTS);
    }
}




static const Macro MACROS[] = {
    
    {"test_type",   "Test: Type Text",       "⌨",  "DELAY 500\nSTRING BLE HID working!\nENTER"},
    {"test_ctrl_a", "Test: Ctrl+A",          "🔬", "DELAY 300\nCTRL a"},

    
    {"lock",        "Lock Screen",           "🔒", "DELAY 300\nGUI l"},
    {"shutdown_w",  "Shutdown (Win)",        "⚡",  "DELAY 300\nGUI r\nDELAY 800\nSTRING shutdown /s /t 0\nENTER"},
    {"restart_w",   "Restart (Win)",         "🔄", "DELAY 300\nGUI r\nDELAY 800\nSTRING shutdown /r /t 0\nENTER"},
    {"shutdown_l",  "Shutdown (Linux)",      "⚡",  "DELAY 300\nCTRL ALT t\nDELAY 800\nSTRING sudo shutdown -h now\nENTER"},
    {"restart_l",   "Restart (Linux)",       "🔄", "DELAY 300\nCTRL ALT t\nDELAY 800\nSTRING sudo reboot\nENTER"},
    {"sleep_w",     "Sleep (Win)",           "💤", "DELAY 300\nGUI x\nDELAY 600\nSTRING u\nDELAY 300\nSTRING s"},
    {"desktop",     "Show Desktop",          "🖥",  "DELAY 300\nGUI d"},
    {"task_mgr",    "Task Manager",          "📊", "DELAY 300\nCTRL SHIFT ESCAPE"},
    {"close_win",   "Close Window",          "❌", "DELAY 300\nALT F4"},

    
    {"wifi_on_w",   "WiFi On (Win)",         "📶", "DELAY 300\nGUI r\nDELAY 800\nSTRING ms-settings:network-wifi\nENTER\nDELAY 1500"},
    {"wifi_con_w",  "WiFi Connect (Win)",    "🌐",
     "DELAY 300\nGUI r\nDELAY 800\nSTRING cmd\nENTER\nDELAY 800\n"
     "STRING netsh wlan connect name=\"{{WIFI_SSID}}\"\nENTER\nDELAY 500\nSTRING exit\nENTER"},
    {"wifi_add_w",  "WiFi Add+Connect (Win)","📡",
     "DELAY 300\nGUI r\nDELAY 800\nSTRING powershell -Command "
     "\"netsh wlan add profile filename=\\\"$env:TEMP\\\\wifi.xml\\\"; "
     "netsh wlan connect name='{{WIFI_SSID}}'\"\nENTER\nDELAY 500"},

    
    {"wifi_on_l",   "WiFi On (Linux)",       "📶", "DELAY 300\nCTRL ALT t\nDELAY 800\nSTRING nmcli radio wifi on\nENTER"},
    {"wifi_con_l",  "WiFi Connect (Linux)",  "🌐",
     "DELAY 300\nCTRL ALT t\nDELAY 800\n"
     "STRING nmcli dev wifi connect \"{{WIFI_SSID}}\" password \"{{WIFI_PASS}}\"\nENTER"},

    
    {"notepad",     "Notepad",               "📝", "DELAY 300\nGUI r\nDELAY 800\nSTRING notepad\nENTER\nDELAY 800"},
    {"calc",        "Calculator",            "🔢", "DELAY 300\nGUI r\nDELAY 800\nSTRING calc\nENTER\nDELAY 800"},
    {"cmd",         "CMD",                   "🖥",  "DELAY 300\nGUI r\nDELAY 800\nSTRING cmd\nENTER"},
    {"term_l",      "Terminal (Linux)",      "🐧", "DELAY 300\nCTRL ALT t"},
    {"browser",     "Open Browser",          "🌐", "DELAY 300\nGUI r\nDELAY 800\nSTRING https://www.google.com\nENTER"},
    {"explorer",    "File Explorer",         "📁", "DELAY 300\nGUI e\nDELAY 500"},

    
    {"copy",        "Copy",                  "📋", "DELAY 200\nCTRL c"},
    {"paste",       "Paste",                 "📌", "DELAY 200\nCTRL v"},
    {"cut",         "Cut",                   "✂",  "DELAY 200\nCTRL x"},
    {"undo",        "Undo",                  "↩",  "DELAY 200\nCTRL z"},
    {"save",        "Save",                  "💾", "DELAY 200\nCTRL s"},
    {"select_all",  "Select All",            "☑",  "DELAY 200\nCTRL a"},
    {"new_tab",     "New Tab",               "➕", "DELAY 200\nCTRL t"},
    {"close_tab",   "Close Tab",             "✖",  "DELAY 200\nCTRL w"},
    {"refresh",     "Refresh",               "🔄", "DELAY 200\nF5"},
    {"fullscreen",  "Fullscreen",            "⛶",  "DELAY 200\nF11"},
    {"screenshot",  "PrintScreen",           "📷", "DELAY 200\nPRINTSCREEN"},
    {"snip",        "Snipping Tool",         "📸", "DELAY 300\nGUI SHIFT s"},
    {"alt_tab",     "Alt+Tab",               "🔁", "DELAY 200\nALT TAB"},

    {nullptr,nullptr,nullptr,nullptr}
};

static String getMacroScript(const char* id, const char* ssid, const char* pass) {
    for (int i = 0; MACROS[i].id; i++) {
        if (strcmp(MACROS[i].id, id) == 0) {
            String s = String(MACROS[i].script);
            s.replace("{{WIFI_SSID}}", ssid);
            s.replace("{{WIFI_PASS}}", pass);
            return s;
        }
    }
    return "";
}




static String hostsJson() {
    String j = "["; bool first = true;
    unsigned long now_s = millis()/1000;
    for (int i = 0; i < MAX_HOSTS; i++) {
        if (!hosts[i].active) continue;
        if (!first) j += ","; first = false;
        unsigned long age_s = (now_s >= hosts[i].connected_s)
                               ? (now_s - hosts[i].connected_s) : 0;
        j += "{\"conn_id\":"  + String(hosts[i].conn_id)  +
             ",\"addr\":\""   + String(hosts[i].addr)      + "\"" +
             ",\"subscribed\":"+ String(hosts[i].subscribed ? "true" : "false") +
             ",\"age_s\":"    + String(age_s) +            
             ",\"label\":\""  + String(hosts[i].label)     + "\"}";
    }
    return j + "]";
}

static String macrosJson() {
    String j = "["; bool first = true;
    for (int i = 0; MACROS[i].id; i++) {
        if (!first) j += ","; first = false;
        j += "{\"id\":\""    + String(MACROS[i].id)    + "\"" +
             ",\"label\":\"" + String(MACROS[i].label) + "\"" +
             ",\"emoji\":\"" + String(MACROS[i].emoji) + "\"}";
    }
    return j + "]";
}

static String blocklistJson() {
    block_cleanup_expired();
    String j = "["; bool first = true;
    unsigned long now = millis()/1000;
    for (int i = 0; i < MAX_BLOCKED; i++) {
        if (!blocklist[i].active) continue;
        if (!first) j += ","; first = false;
        unsigned long rem = (blocklist[i].blocked_until_s > now)
                             ? (blocklist[i].blocked_until_s - now) : 0;
        j += "{\"addr\":\"" + String(blocklist[i].addr) + "\",\"remaining_s\":" + String(rem) + "}";
    }
    return j + "]";
}




static const char PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Dex BLE</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:system-ui,sans-serif;background:#0d1117;color:#c9d1d9;padding:10px;font-size:14px}
h1{color:#e6edf3;font-size:1.05rem;margin-bottom:10px}
.card{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:12px;margin-bottom:9px}
.card h2{font-size:.72rem;color:#8b949e;text-transform:uppercase;letter-spacing:.07em;margin-bottom:10px}
.row{display:flex;justify-content:space-between;align-items:center;padding:4px 0;border-bottom:1px solid #21262d;font-size:.83rem}
.row:last-child{border-bottom:none}
.lbl{color:#8b949e}
.pill{display:inline-block;padding:2px 9px;border-radius:9px;font-size:.72rem;font-weight:600}
.on{background:#1a4a2e;color:#3fb950}.off{background:#3d1515;color:#f85149}.warn{background:#2d2208;color:#e3b341}
button{background:#21262d;color:#c9d1d9;border:1px solid #30363d;border-radius:5px;padding:6px 12px;font-size:.8rem;cursor:pointer;margin:2px}
button:hover{background:#30363d}
.run{background:#1f6030;border-color:#3fb950;color:#3fb950}.run:hover{background:#238636}
.danger{background:#4a1010;border-color:#f85149;color:#f85149}.danger:hover{background:#8b1a1a}
.blue{background:#0d2a6b;border-color:#388bfd;color:#58a6ff}.blue:hover{background:#1158c7}
input,select,textarea{width:100%;padding:6px 8px;margin:3px 0 8px;border-radius:5px;border:1px solid #30363d;background:#0d1117;color:#c9d1d9;font-size:.82rem}
textarea{font-family:monospace;resize:vertical;min-height:110px}
.macro-wrap{display:flex;flex-wrap:wrap;gap:4px;margin:6px 0}
.macro-wrap button{padding:4px 8px;font-size:.74rem;margin:0}
.log{background:#0a0c10;border:1px solid #21262d;border-radius:5px;padding:8px;font-family:monospace;font-size:.73rem;color:#8b949e;max-height:160px;overflow-y:auto;white-space:pre;margin-top:6px}
.msg{font-size:.74rem;margin-top:4px;min-height:1em}
.ok{color:#3fb950}.err{color:#f85149}.info{color:#58a6ff}
.host-item{display:flex;justify-content:space-between;align-items:center;padding:8px 10px;background:#0d1117;border:1px solid #30363d;border-radius:5px;margin-bottom:4px;cursor:pointer;transition:border-color .15s}
.host-item:hover{border-color:#58a6ff}
.host-item.selected{border-color:#3fb950;background:#1a4a2e22}
.host-addr{font-family:monospace;font-size:.73rem;color:#6e7681}
.stitle{font-size:.72rem;color:#8b949e;text-transform:uppercase;letter-spacing:.06em;margin:10px 0 4px}
.wifi-fields{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:8px}
@media(max-width:480px){.wifi-fields{grid-template-columns:1fr}}
</style></head><body>
<h1>&#x1F4E1; Dex BLE</h1>

<!-- ── Status card ────────────────────────────────────────────────────────── -->
<div class="card">
  <h2>Status</h2>
  <div class="row"><span class="lbl">BLE Hosts</span><span id="s_count" class="pill off">0</span></div>
  <div class="row"><span class="lbl">Target</span><span id="s_target" style="font-size:.78rem;color:#3fb950">All hosts</span></div>
  <div class="row"><span class="lbl">Input Pipe</span><span id="s_pipe" class="pill off">Not ready</span></div>
  <div class="row"><span class="lbl">Script</span><span id="s_script" class="pill off">Idle</span></div>
  <div class="row"><span class="lbl">Uptime</span><span id="s_up">—</span></div>
  <div class="row"><span class="lbl">BLE Name</span><span id="s_ble" style="font-size:.78rem;color:#6e7681">—</span></div>
  <div class="row"><span class="lbl">AP SSID</span><span id="s_ap" style="font-size:.78rem;color:#6e7681">—</span></div>
</div>

<!-- ── Connected hosts ────────────────────────────────────────────────────── -->
<div class="card">
  <h2>&#x1F4BB; Connected Hosts
    <button onclick="refreshHosts()" style="float:right;padding:2px 7px;font-size:.7rem;margin:0">↻</button>
  </h2>
  <p style="font-size:.75rem;color:#8b949e;margin-bottom:8px">
    Click a host to send HID only to that device. Click again to deselect (sends to all).
  </p>
  <div id="host_list"><div style="color:#6e7681;font-size:.82rem;padding:4px 0">No hosts connected</div></div>
</div>

<!-- ── Blocked devices ────────────────────────────────────────────────────── -->
<div class="card">
  <h2>&#x1F6AB; Blocked Devices
    <button onclick="refreshBlocklist()" style="float:right;padding:2px 7px;font-size:.7rem;margin:0">↻</button>
  </h2>
  <p style="font-size:.75rem;color:#8b949e;margin-bottom:8px">
    Kicked devices are blocked from reconnecting until the timer expires.
  </p>
  <div id="block_list"><div style="color:#6e7681;font-size:.78rem;padding:4px 0">No blocked devices</div></div>
  <div class="stitle">Default Kick-Block Duration</div>
  <div style="display:flex;gap:6px;align-items:center">
    <input id="set_block_secs" type="number" min="0" max="86400" value="120" style="flex:1;margin:0">
    <span style="font-size:.76rem;color:#8b949e">seconds</span>
    <button class="blue" onclick="saveBlockDuration()" style="margin:0">Save</button>
  </div>
  <p style="font-size:.7rem;color:#6e7681;margin-top:4px">Set to 0 to allow instant reconnect after kick.</p>
  <div class="msg" id="msg_block"></div>
</div>

<!-- ── Script runner ──────────────────────────────────────────────────────── -->
<div class="card">
  <h2>&#x1F916; Script Runner</h2>

  <div class="stitle">&#x1F3AF; Target Device</div>
  <select id="target_select" onchange="onTargetChange()">
    <option value="-1">All connected hosts</option>
  </select>

  <div class="stitle">WiFi Credentials (substituted into WiFi macros)</div>
  <div class="wifi-fields">
    <div>
      <label style="font-size:.74rem;color:#8b949e">WiFi SSID</label>
      <input id="wifi_ssid" type="text" placeholder="MyHomeWiFi">
    </div>
    <div>
      <label style="font-size:.74rem;color:#8b949e">WiFi Password</label>
      <input id="wifi_pass" type="password" placeholder="password123">
    </div>
  </div>

  <div class="stitle">Macro Library</div>
  <div class="macro-wrap" id="macro_grid"></div>

  <div class="stitle">Script Editor (Ducky Script)</div>
  <textarea id="ed" placeholder="REM Ducky Script&#10;DELAY 500&#10;STRING hello&#10;ENTER"></textarea>
  <div style="display:flex;gap:5px;flex-wrap:wrap;margin-top:4px">
    <button class="run"    onclick="runScript()">&#x25B6; Run</button>
    <button class="danger" onclick="stopScript()">&#x23F9; Stop</button>
    <button onclick="document.getElementById('ed').value=''">&#x1F5D1; Clear</button>
    <button onclick="clearLog()" style="margin-left:auto">&#x1F9F9; Clear Log</button>
  </div>
  <div class="msg" id="msg"></div>
  <div class="log" id="log">No output yet.</div>
</div>

<!-- ── Settings ───────────────────────────────────────────────────────────── -->
<div class="card">
  <h2>&#x2699;&#xFE0F; Settings</h2>

  <div class="stitle">WiFi Access Point (reboot to apply)</div>
  <label style="font-size:.74rem;color:#8b949e">AP SSID</label>
  <input id="set_ap_ssid" type="text" maxlength="31">
  <label style="font-size:.74rem;color:#8b949e">AP Password (≥8 chars; blank = keep current)</label>
  <input id="set_ap_pass" type="password" maxlength="63">
  <button class="blue" onclick="saveWifi()" style="width:100%;margin:2px 0 10px">&#x1F4BE; Save AP Settings</button>
  <div class="msg" id="msg_wifi"></div>

  <div class="stitle">Bluetooth Name (reboot to apply; clears all bonds)</div>
  <label style="font-size:.74rem;color:#8b949e">BLE Device Name</label>
  <input id="set_ble" type="text" maxlength="31">
  <button class="blue" onclick="saveBLE()" style="width:100%;margin:2px 0 10px">&#x1F4BE; Save BLE Name</button>
  <div class="msg" id="msg_ble"></div>

  <div class="stitle">Danger Zone</div>
  <button class="danger"
    onclick="if(confirm('Reboot ESP32 now?'))fetch('/api/reboot',{method:'POST'})"
    style="width:100%;margin-top:2px">&#x1F501; Reboot ESP32</button>
</div>

<div style="font-size:.68rem;color:#6e7681;text-align:center;margin-top:8px;padding-bottom:6px">
  Ducky: STRING · DELAY · ENTER · TAB · ESC · GUI · CTRL · ALT · SHIFT · F1-F12 · arrows · PRINTSCREEN · REPEAT · REM · DEFAULT_DELAY
</div>

<script>
// ── State ────────────────────────────────────────────────────────────────────
let selConn = -1;          // currently selected host conn_id (-1 = all)
let macros  = [];          // cached macro list from /api/macros
let defaultBlockSecs = 120;

// ── Utilities ────────────────────────────────────────────────────────────────
function fmtUp(s)  { const h=Math.floor(s/3600),m=Math.floor((s%3600)/60),ss=s%60; return(h?h+'h ':'')+(m?m+'m ':'')+ss+'s'; }
function fmtAge(s) { if(s<60) return s+'s ago'; if(s<3600) return Math.floor(s/60)+'m ago'; return Math.floor(s/3600)+'h ago'; }
function fmtDur(s) { if(s<=0) return 'no block'; if(s<60) return s+'s'; if(s<3600) return Math.round(s/60)+'m'; return Math.round(s/3600)+'h'; }
function esc(s)    { return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;'); }
function setMsg(id,text,cls){ const el=document.getElementById(id); el.textContent=text; el.className='msg '+(cls||'info'); setTimeout(()=>{el.textContent='';el.className='msg';},5000); }

// ── Status polling ────────────────────────────────────────────────────────────
async function refreshStatus() {
  // console.log('[STATUS] polling /api/status');
  try {
    const d = await fetch('/api/status').then(r=>r.json());

    // Host count pill
    const cp = document.getElementById('s_count');
    cp.textContent = d.host_count + ' connected';
    cp.className   = 'pill ' + (d.host_count > 0 ? 'on' : 'off');
    // console.log('[STATUS] host_count=' + d.host_count);

    // Target label
    document.getElementById('s_target').textContent = selConn===-1 ? 'All hosts' : 'Host #'+selConn;

    // Input pipe pill
    const pp = document.getElementById('s_pipe');
    pp.textContent = d.target_ready ? 'Ready' : (d.host_count > 0 ? 'Waiting…' : 'No host');
    pp.className   = 'pill ' + (d.target_ready ? 'on' : (d.host_count > 0 ? 'warn' : 'off'));

    // Script pill
    const sp = document.getElementById('s_script');
    sp.textContent = d.running ? 'Running…' : 'Idle';
    sp.className   = 'pill ' + (d.running ? 'warn' : 'off');

    // Labels
    document.getElementById('s_up').textContent  = fmtUp(d.uptime_s);
    document.getElementById('s_ble').textContent  = d.ble_name  || '—';
    document.getElementById('s_ap').textContent   = d.ap_ssid   || '—';

    // Block duration sync
    if (typeof d.default_block_secs === 'number') defaultBlockSecs = d.default_block_secs;

    // Pre-fill settings fields on first load
    if (!document.getElementById('set_ble').dataset.loaded) {
      document.getElementById('set_ble').value         = d.ble_name  || '';
      document.getElementById('set_ap_ssid').value     = d.ap_ssid   || '';
      document.getElementById('set_block_secs').value  = defaultBlockSecs;
      document.getElementById('set_ble').dataset.loaded = '1';
      // console.log('[STATUS] Settings fields pre-filled');
    }
  } catch(e) {
    // console.error('[STATUS] fetch failed:', e);
  }
}

// ── Host list ─────────────────────────────────────────────────────────────────
async function refreshHosts() {
  // console.log('[HOSTS] polling /api/hosts');
  try {
    const hosts = await fetch('/api/hosts').then(r=>r.json());
    const list  = document.getElementById('host_list');
    const sel   = document.getElementById('target_select');

    if (!hosts || hosts.length === 0) {
      list.innerHTML = '<div style="color:#6e7681;font-size:.82rem;padding:4px 0">No hosts connected</div>';
      sel.innerHTML  = '<option value="-1">All connected hosts</option>';
      if (selConn !== -1) selConn = -1;
      // console.log('[HOSTS] no hosts');
      return;
    }

    // Render host cards
    list.innerHTML = hosts.map(h => {
      const isSel      = (selConn === h.conn_id);
      const labelText  = h.label && h.label.length > 0
                         ? esc(h.label)
                         : '<span style="color:#6e7681;font-style:italic">unlabeled</span>';
      return `<div class="host-item${isSel?' selected':''}" onclick="selectHost(${h.conn_id})">
        <div style="flex:1">
          <div style="font-weight:600;font-size:.85rem">
            Host #${h.conn_id} — ${labelText}
            <span class="pill ${h.subscribed?'on':'warn'}" style="margin-left:5px;font-size:.7rem">
              ${h.subscribed ? '✓ Ready' : '⏳ Waiting'}
            </span>
            ${isSel ? '<span class="pill on" style="margin-left:4px;font-size:.7rem">★ Target</span>' : ''}
          </div>
          <div class="host-addr">${esc(h.addr)}</div>
          <div style="font-size:.72rem;color:#6e7681">connected ${fmtAge(h.age_s)}</div>
          <div style="margin-top:5px;display:flex;gap:3px;flex-wrap:wrap" onclick="event.stopPropagation()">
            <button style="padding:2px 7px;font-size:.68rem;margin:0" onclick="setLabel(${h.conn_id},'🪟 Windows PC')">🪟 Windows</button>
            <button style="padding:2px 7px;font-size:.68rem;margin:0" onclick="setLabel(${h.conn_id},'🐧 Linux')">🐧 Linux</button>
            <button style="padding:2px 7px;font-size:.68rem;margin:0" onclick="setLabel(${h.conn_id},'🤖 Android')">🤖 Android</button>
            <button style="padding:2px 7px;font-size:.68rem;margin:0" onclick="setLabel(${h.conn_id},'📱 iOS')">📱 iOS</button>
            <button style="padding:2px 7px;font-size:.68rem;margin:0" onclick="setLabel(${h.conn_id},'🍎 Mac')">🍎 Mac</button>
            <button style="padding:2px 7px;font-size:.68rem;margin:0" onclick="customLabel(${h.conn_id})">✏ Custom</button>
          </div>
        </div>
        <button class="danger" style="padding:3px 8px;font-size:.72rem;white-space:nowrap"
          onclick="event.stopPropagation();kickHost(${h.conn_id})"
          title="Kick and block for ${fmtDur(defaultBlockSecs)}">
          Kick (${fmtDur(defaultBlockSecs)})
        </button>
      </div>`;
    }).join('');
    // console.log('[HOSTS] rendered', hosts.length, 'host cards');

    // Rebuild target dropdown
    let opts = '<option value="-1">All connected hosts</option>';
    hosts.forEach(h => {
      const tag  = h.label && h.label.length > 0 ? h.label : 'unlabeled';
      const text = `Host #${h.conn_id} — ${tag} — ${h.addr} ${h.subscribed?'(Ready)':'(Waiting)'}`;
      opts += `<option value="${h.conn_id}"${selConn===h.conn_id?' selected':''}>${esc(text)}</option>`;
    });
    sel.innerHTML = opts;
    if (selConn === -1) sel.value = '-1';

  } catch(e) {
    // console.error('[HOSTS] fetch failed:', e);
  }
}

// ── Host selection ────────────────────────────────────────────────────────────
function selectHost(cid) {
  selConn = (selConn === cid) ? -1 : cid;   // toggle: click selected host to deselect
  // console.log('[TARGET] selConn now', selConn);
  applyTarget();
  refreshHosts();
  refreshStatus();
}

function onTargetChange() {
  selConn = parseInt(document.getElementById('target_select').value, 10);
  // console.log('[TARGET] dropdown changed to', selConn);
  applyTarget();
  refreshHosts();
  refreshStatus();
}

function applyTarget() {
  fetch('/api/target', {method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:'conn_id='+selConn});
  // console.log('[TARGET] POST /api/target conn_id=' + selConn);
}

// ── Host actions: kick, label ─────────────────────────────────────────────────
async function kickHost(cid) {
  if (!confirm(`Disconnect host #${cid} and block reconnects for ${fmtDur(defaultBlockSecs)}?`)) return;
  // console.log('[KICK] kicking conn_id=' + cid);
  await fetch('/api/kick', {method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:'conn_id='+cid+'&block_secs='+defaultBlockSecs});
  if (selConn === cid) selConn = -1;
  setTimeout(() => { refreshHosts(); refreshBlocklist(); }, 800);
}

async function setLabel(cid, label) {
  // console.log('[LABEL] setting conn_id=' + cid + ' label=' + label);
  await fetch('/api/label', {method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:'conn_id='+cid+'&label='+encodeURIComponent(label)});
  setTimeout(refreshHosts, 300);
}

async function customLabel(cid) {
  const label = prompt('Enter a custom device name (e.g. "My Laptop", "Office PC"):');
  if (label === null || label.trim() === '') return;
  await setLabel(cid, label.trim());
}

// ── Blocklist ─────────────────────────────────────────────────────────────────
async function refreshBlocklist() {
  // console.log('[BLOCKLIST] polling /api/blocklist');
  try {
    const list = await fetch('/api/blocklist').then(r=>r.json());
    const el   = document.getElementById('block_list');
    if (!list || list.length === 0) {
      el.innerHTML = '<div style="color:#6e7681;font-size:.78rem;padding:4px 0">No blocked devices</div>';
      return;
    }
    el.innerHTML = list.map(b =>
      `<div class="host-item" style="cursor:default">
        <div>
          <div class="host-addr">${esc(b.addr)}</div>
          <div style="font-size:.72rem;color:#e3b341">blocked for ${fmtDur(b.remaining_s)} more</div>
        </div>
        <button class="blue" style="padding:3px 8px;font-size:.72rem"
          onclick="unblockAddr('${esc(b.addr)}')">Unblock</button>
      </div>`
    ).join('');
    // console.log('[BLOCKLIST] rendered', list.length, 'entries');
  } catch(e) {
    // console.error('[BLOCKLIST] fetch failed:', e);
  }
}

async function unblockAddr(addr) {
  // console.log('[BLOCKLIST] unblocking', addr);
  await fetch('/api/unblock', {method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:'addr='+encodeURIComponent(addr)});
  setTimeout(refreshBlocklist, 400);
}

async function saveBlockDuration() {
  const v = parseInt(document.getElementById('set_block_secs').value, 10) || 120;
  // console.log('[BLOCKLIST] saving default_secs=' + v);
  const d = await fetch('/api/block_settings', {method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:'default_secs='+v}).then(r=>r.json());
  defaultBlockSecs = d.default_secs;
  setMsg('msg_block', '✓ Block duration set to ' + fmtDur(defaultBlockSecs), 'ok');
  refreshHosts();
}

// ── Macro library ─────────────────────────────────────────────────────────────
async function loadMacros() {
  // console.log('[MACROS] loading /api/macros');
  macros = await fetch('/api/macros').then(r=>r.json());
  document.getElementById('macro_grid').innerHTML = macros.map(m =>
    `<button onclick="loadMacro('${esc(m.id)}')">${m.emoji} ${esc(m.label)}</button>`
  ).join('');
  // console.log('[MACROS] loaded', macros.length, 'macros');
}

async function loadMacro(id) {
  const ssid = document.getElementById('wifi_ssid').value.trim() || 'MyWiFi';
  const pass = document.getElementById('wifi_pass').value.trim() || '';
  // console.log('[MACRO] loading id=' + id + ' ssid=' + ssid);
  const r = await fetch('/api/macro?id='+encodeURIComponent(id)+'&ssid='+encodeURIComponent(ssid)+'&pass='+encodeURIComponent(pass));
  const d = await r.json();
  if (d.script) document.getElementById('ed').value = d.script;
}

// ── Script runner ─────────────────────────────────────────────────────────────
async function runScript() {
  const script = document.getElementById('ed').value.trim();
  if (!script) { setMsg('msg','Script is empty','err'); return; }

  // console.log('[SCRIPT] checking status before run');
  const st = await fetch('/api/status').then(r=>r.json());
  if (st.host_count === 0)   { setMsg('msg','No BLE host connected','err');   return; }
  if (!st.target_ready)      { setMsg('msg','Input pipe not ready — wait a moment after connecting','err'); return; }
  if (st.running)            { setMsg('msg','Script already running','err');   return; }

  // console.log('[SCRIPT] POSTing script (' + script.length + ' chars)');
  const r = await fetch('/api/run', {method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:'script='+encodeURIComponent(script)});
  const d = await r.json();
  setMsg('msg', d.ok ? '▶ Started' : '✗ '+d.error, d.ok ? 'ok' : 'err');
  // console.log('[SCRIPT] start result:', d);
}

async function stopScript() {
  // console.log('[SCRIPT] sending stop');
  await fetch('/api/stop', {method:'POST'});
  setMsg('msg','Stop requested','info');
}

async function refreshLog() {
  try {
    const d  = await fetch('/api/log').then(r=>r.json());
    const el = document.getElementById('log');
    el.textContent = (d.log || '').trim() || '(empty)';
    el.scrollTop   = el.scrollHeight;
  } catch(e) {
    // console.error('[LOG] fetch failed:', e);
  }
}

async function clearLog() {
  await fetch('/api/log/clear', {method:'POST'});
  document.getElementById('log').textContent = '(cleared)';
}

// ── Settings ──────────────────────────────────────────────────────────────────
async function saveWifi() {
  const body = new URLSearchParams({
    ssid: document.getElementById('set_ap_ssid').value,
    pass: document.getElementById('set_ap_pass').value,
  });
  // console.log('[SETTINGS] saving WiFi ssid=' + body.get('ssid'));
  const d = await fetch('/api/settings/wifi', {method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded'}, body}).then(r=>r.json());
  setMsg('msg_wifi', d.ok ? '✓ Saved to flash' : '✗ '+(d.error||'Error'), d.ok?'ok':'err');
  if (d.ok && confirm('WiFi settings saved. Reboot now to apply?\nYou will need to reconnect to the new WiFi name/password.')) {
    await fetch('/api/reboot', {method:'POST'});
    setMsg('msg_wifi','Rebooting… reconnect to new WiFi in ~10 s','info');
  }
}

async function saveBLE() {
  const body = new URLSearchParams({ name: document.getElementById('set_ble').value });
  // console.log('[SETTINGS] saving BLE name=' + body.get('name'));
  const d = await fetch('/api/settings/ble', {method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded'}, body}).then(r=>r.json());
  setMsg('msg_ble', d.ok ? '✓ Saved to flash' : '✗ '+(d.error||'Error'), d.ok?'ok':'err');
  if (d.ok && confirm('BLE name saved. Reboot now?\n\nAll bonds will be cleared — every paired device must re-pair under the new name.')) {
    await fetch('/api/reboot', {method:'POST'});
    setMsg('msg_ble','Rebooting… re-pair all devices with the new name','info');
  }
}

// ── Bootstrap ─────────────────────────────────────────────────────────────────
// console.log('[BOOT] portal initialising');
loadMacros();
refreshStatus();
refreshHosts();
refreshLog();
refreshBlocklist();
setInterval(refreshStatus,   2000);
setInterval(refreshHosts,    3000);
setInterval(refreshLog,      2000);
setInterval(refreshBlocklist,5000);
// console.log('[BOOT] polling started');
</script></body></html>
)HTML";




static void h_root()   { web.send(200, "text/html", PAGE); }

static void h_status() {
    char buf[360];
    snprintf(buf, sizeof(buf),
        "{\"host_count\":%d,\"target_ready\":%s,\"any_subscribed\":%s,"
        "\"running\":%s,\"uptime_s\":%lu,"
        "\"ble_name\":\"%s\",\"ap_ssid\":\"%s\",\"target_conn\":%d,"
        "\"default_block_secs\":%lu}",
        host_count(),
        target_ready()      ? "true" : "false",
        any_subscribed()    ? "true" : "false",
        script_running      ? "true" : "false",
        millis()/1000UL,
        cfg.ble_name, cfg.ap_ssid, target_conn_id,
        default_block_secs);
    web.send(200, "application/json", buf);
}

static void h_hosts()  { web.send(200, "application/json", hostsJson()); }
static void h_macros() { web.send(200, "application/json", macrosJson()); }

static void h_macro() {
    String id   = web.arg("id");
    String ssid = web.arg("ssid"); if (ssid.isEmpty()) ssid = "MyWiFi";
    String pass = web.arg("pass");
    String s    = getMacroScript(id.c_str(), ssid.c_str(), pass.c_str());
    if (s.isEmpty()) { web.send(404, "application/json", "{\"error\":\"not found\"}"); return; }
    s.replace("\\","\\\\"); s.replace("\"","\\\""); s.replace("\n","\\n"); s.replace("\r","");
    web.send(200, "application/json", "{\"script\":\"" + s + "\"}");
}

static void h_target() {
    target_conn_id = web.arg("conn_id").toInt();
    Serial.printf("[WEB] /api/target  conn_id=%d (%s)\n",
                  target_conn_id, target_conn_id == -1 ? "ALL" : "specific");
    web.send(200, "application/json", "{\"ok\":true}");
}

static void h_set_label() {
    unsigned short cid   = (unsigned short)web.arg("conn_id").toInt();
    String         label = web.arg("label");
    if (label.length() > 23) label = label.substring(0, 23);
    Host* h = host_find(cid);
    if (!h) { web.send(404, "application/json", "{\"error\":\"host not found\"}"); return; }
    label.toCharArray(h->label, sizeof(h->label));
    label_save(h->addr, h->label);
    Serial.printf("[WEB] /api/label  conn=%u  addr=%s  label='%s'\n", cid, h->addr, h->label);
    web.send(200, "application/json", "{\"ok\":true}");
}

static void h_kick() {
    unsigned short cid  = (unsigned short)web.arg("conn_id").toInt();
    unsigned long  secs = web.hasArg("block_secs")
                          ? (unsigned long)web.arg("block_secs").toInt()
                          : default_block_secs;
    Host*  h    = host_find(cid);
    String addr = h ? String(h->addr) : "";
    Serial.printf("[WEB] /api/kick  conn=%u  addr=%s  block_secs=%lu\n",
                  cid, addr.c_str(), secs);
    if (addr.length() > 0 && secs > 0) block_add(addr.c_str(), secs);
    if (pServer) pServer->disconnect(cid);
    web.send(200, "application/json", "{\"ok\":true}");
}

static void h_unblock() {
    String addr = web.arg("addr");
    if (addr.isEmpty()) { web.send(400, "application/json", "{\"error\":\"addr required\"}"); return; }
    addr.toUpperCase();
    block_remove(addr.c_str());
    Serial.printf("[WEB] /api/unblock  addr=%s\n", addr.c_str());
    web.send(200, "application/json", "{\"ok\":true}");
}

static void h_blocklist() { web.send(200, "application/json", blocklistJson()); }

static void h_block_settings() {
    if (web.hasArg("default_secs")) {
        long v = web.arg("default_secs").toInt();
        if (v >= 0 && v <= 86400) {
            default_block_secs = (unsigned long)v;
            prefs.putULong("block_secs", default_block_secs);
            Serial.printf("[WEB] /api/block_settings  default_secs=%lu\n", default_block_secs);
        }
    }
    web.send(200, "application/json",
             "{\"ok\":true,\"default_secs\":" + String(default_block_secs) + "}");
}

static void h_log() {
    String out = "{\"log\":\"";
    for (int i = 0; i < slog_len; i++) {
        char c = slog_buf[i];
        if      (c == '\\') out += "\\\\";
        else if (c == '"')  out += "\\\"";
        else if (c == '\n') out += "\\n";
        else if (c != '\r') out += c;
    }
    out += "\"}";
    web.send(200, "application/json", out);
}

static void h_log_clear() {
    slog_buf[0] = '\0'; slog_len = 0;
    Serial.println("[WEB] /api/log/clear");
    web.send(200, "application/json", "{\"ok\":true}");
}

static void h_run() {
    if (!web.hasArg("script"))  { web.send(400,"application/json","{\"error\":\"missing script\"}");  return; }
    if (host_count() == 0)      { web.send(400,"application/json","{\"error\":\"no BLE host\"}");     return; }
    if (!target_ready())        { web.send(400,"application/json","{\"error\":\"pipe not ready\"}");   return; }
    if (script_running)         { web.send(400,"application/json","{\"error\":\"already running\"}");  return; }
    String s = web.arg("script");
    if (!runScript(s.c_str()))  { web.send(503,"application/json","{\"error\":\"failed to start\"}");  return; }
    Serial.printf("[WEB] /api/run  %d chars  target=%d\n", s.length(), target_conn_id);
    web.send(200, "application/json", "{\"ok\":true}");
}

static void h_stop() {
    script_abort = true; script_running = false;
    Serial.println("[WEB] /api/stop");
    web.send(200, "application/json", "{\"ok\":true}");
}

static void h_wifi() {
    String ssid = web.arg("ssid"), pass = web.arg("pass");
    if (ssid.isEmpty()) { web.send(400,"application/json","{\"error\":\"ssid required\"}"); return; }
    if (pass.length() > 0 && pass.length() < 8) {
        web.send(400,"application/json","{\"error\":\"password must be 8+ chars\"}"); return;
    }
    ssid.toCharArray(cfg.ap_ssid, sizeof(cfg.ap_ssid));
    if (pass.length() >= 8) pass.toCharArray(cfg.ap_pass, sizeof(cfg.ap_pass));
    cfg_save();
    Serial.printf("[WEB] /api/settings/wifi  ssid='%s'\n", cfg.ap_ssid);
    web.send(200, "application/json", "{\"ok\":true}");
}

static void h_ble() {
    String name = web.arg("name");
    if (name.isEmpty()) { web.send(400,"application/json","{\"error\":\"name required\"}"); return; }
    name.toCharArray(cfg.ble_name, sizeof(cfg.ble_name));
    cfg_save();
    
    prefs.putBool("clear_bonds_next", true);
    Serial.printf("[WEB] /api/settings/ble  name='%s'  bonds will clear on next reboot\n", cfg.ble_name);
    web.send(200, "application/json", "{\"ok\":true}");
}

static void h_reboot() {
    web.send(200, "application/json", "{\"ok\":true}");
    delay(200);
    Serial.println("[SYS] Graceful reboot — disconnecting all BLE hosts first");
    if (pServer) {
        for (int i = 0; i < MAX_HOSTS; i++) {
            if (hosts[i].active) {
                Serial.printf("[SYS]   disconnecting conn=%u\n", hosts[i].conn_id);
                pServer->disconnect(hosts[i].conn_id);
            }
        }
    }
    delay(300);
    Serial.println("[SYS] Rebooting now");
    Serial.flush();
    ESP.restart();
}




void setup() {
    
    LOG.begin(115200);
    delay(1500);   

    
    for (int _b = 0; _b < 2; _b++) {
        Serial.println();
        Serial.println("==============================================");
        Serial.println("  ESP32 BLE HID Hub  v8  — booting");
        Serial.println("  Logs on USB CDC (Serial) AND UART GPIO43");
        Serial.println("==============================================");
        delay(400);
    }

    
    cfg_load();
    default_block_secs = prefs.getULong("block_secs", 120);
    Serial.printf("[CFG] block_default=%lus\n", default_block_secs);

    
    memset(hosts,        0, sizeof(hosts));
    memset(blocklist,    0, sizeof(blocklist));
    memset(detect_queue, 0, sizeof(detect_queue));
    Serial.println("[SYS] Host registry and blocklist cleared");

    
    Serial.printf("[BLE] Initialising as '%s'...\n", cfg.ble_name);
    NimBLEDevice::init(cfg.ble_name);

    
    NimBLEDevice::setMTU(517);
    Serial.println("[BLE] Server MTU set to 517");

    
    bool pwrOk = NimBLEDevice::setPower(9);   // +9 dBm
    Serial.printf("[BLE] TX power +9 dBm  ok=%d\n", (int)pwrOk);

    
    if (prefs.getBool("clear_bonds_next", false)) {
        NimBLEDevice::deleteAllBonds();
        prefs.putBool("clear_bonds_next", false);
        Serial.println("[BLE] All bonds cleared (name-change request)");
    }

    
    NimBLEDevice::setSecurityAuth(true, false, false);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    Serial.printf("[BLE] Max connections (compiled-in): %d\n", MAX_HOSTS);

    
    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new SrvCbs());

    pHID = new NimBLEHIDDevice(pServer);
    pHID->setManufacturer("DIY");
    pHID->setPnp(0x02, 0xE502, 0xA111, 0x0100);   
    pHID->setHidInfo(0x00, 0x01);
    pHID->setReportMap((uint8_t*)KB_MAP, sizeof(KB_MAP));
    pKbd = pHID->getInputReport(1);
    pKbd->setCallbacks(&charCbs);
    pHID->setBatteryLevel(100);
    pHID->startServices();
    Serial.println("[BLE] HID services started");

    
    pAdv = NimBLEDevice::getAdvertising();
    NimBLEAdvertisementData advData;
    advData.setFlags(0x06);                               
    advData.setAppearance(0x03C1);                        
    advData.addServiceUUID(pHID->getHidService()->getUUID());
    advData.addServiceUUID(pHID->getBatteryService()->getUUID());
    advData.setName(cfg.ble_name);
    pAdv->setAdvertisementData(advData);

    
    NimBLEAdvertisementData scanData;
    scanData.addTxPower();
    pAdv->setScanResponseData(scanData);
    pAdv->enableScanResponse(true);
    pAdv->setMinInterval(32);   
    pAdv->setMaxInterval(64);   
    pAdv->start();

    
    int pkt = 2 + 4 + 4 + 4 + 2 + (int)strlen(cfg.ble_name);
    Serial.printf("[BLE] Advertising as '%s'  est_pkt=%d/31\n", cfg.ble_name, pkt);
    if (pkt > 31) Serial.println("[BLE] WARNING: adv packet may exceed 31 bytes — shorten BLE name");
    Serial.printf("[BLE] MAC: %s\n", NimBLEDevice::getAddress().toString().c_str());

    
    WiFi.mode(WIFI_AP);
    WiFi.softAP(cfg.ap_ssid, cfg.ap_pass);
    Serial.printf("[WIFI] AP '%s'  →  http://%s/\n",
                  cfg.ap_ssid, WiFi.softAPIP().toString().c_str());

    
    web.on("/",                   HTTP_GET,  h_root);
    web.on("/api/status",         HTTP_GET,  h_status);
    web.on("/api/hosts",          HTTP_GET,  h_hosts);
    web.on("/api/macros",         HTTP_GET,  h_macros);
    web.on("/api/macro",          HTTP_GET,  h_macro);
    web.on("/api/target",         HTTP_POST, h_target);
    web.on("/api/label",          HTTP_POST, h_set_label);
    web.on("/api/kick",           HTTP_POST, h_kick);
    web.on("/api/unblock",        HTTP_POST, h_unblock);
    web.on("/api/blocklist",      HTTP_GET,  h_blocklist);
    web.on("/api/block_settings", HTTP_POST, h_block_settings);
    web.on("/api/log",            HTTP_GET,  h_log);
    web.on("/api/log/clear",      HTTP_POST, h_log_clear);
    web.on("/api/run",            HTTP_POST, h_run);
    web.on("/api/stop",           HTTP_POST, h_stop);
    web.on("/api/settings/wifi",  HTTP_POST, h_wifi);
    web.on("/api/settings/ble",   HTTP_POST, h_ble);
    web.on("/api/reboot",         HTTP_POST, h_reboot);
    web.begin();
    Serial.println("[WEB] Server started  →  http://192.168.4.1/");
    Serial.println("==============================================\n");
}




void loop() {
    
    web.handleClient();

    
    process_deferred_ble_actions();

    
    advertising_watchdog();

    
    static unsigned long lastBeat = 0;
    if (millis() - lastBeat > 10000) {
        lastBeat = millis();
        Serial.printf("[HEARTBEAT] t=%lu  uptime=%lus  hosts=%d/%d  bonds=%d  adv=%d  heap=%u\n",
                      millis(), millis()/1000,
                      host_count(), MAX_HOSTS,
                      NimBLEDevice::getNumBonds(),
                      pAdv ? (int)pAdv->isAdvertising() : -1,
                      ESP.getFreeHeap());
    }

    delay(5);
}
