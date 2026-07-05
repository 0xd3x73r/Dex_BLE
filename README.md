# Dex_BLE
Dex BLE a offensive security HID injection tool built on the ESP32 platform. It emulates a Bluetooth keyboard to silently inject keystrokes into a paired device.


Dex BLE turns a $6 microcontroller into a wireless HID injection tool for RedTeamers or professional ethical hackers.

No USB dongle to plug in. No app to install. No cable in sight.

---

## Feature list

- **Per-host targeting** : send keystrokes to one specific device, or
  broadcast to every subscribed host in one call.
- **Ducky Script engine** : a real interpreter (not a lookup table) running
  in its own FreeRTOS task, with an abort switch exposed on the dashboard (easily customizable).
- **Macro library** : one-tap scripts for shutdown/restart, WiFi
  connect/toggle, and terminal launch, tuned per-OS (Windows / Linux paths
  differ enough that they need separate macros, so Dex BLE ships both).
- **Persistent per-device labels** : labels are keyed by MAC address in NVS,
  so "🎧 Music PC" survives reboots and reconnects automatically.
- **MAC blocklist with timed bans** : kick a misbehaving or unwanted device
  and it can't reconnect until the ban expires.
- **Zero-app control** : the ESP32 runs its own WiFi SoftAP + web server;
  connect once, bookmark the dashboard, done.
- **Supports Windows, macOS, Linux, and Android**
- **Lightweight, portable, and easy to use**
- **35 built-in macros**
- **OS auto-detection**
- **Live serial diagnostics** : heartbeat logging every 10s (host count,
  bond count, advertising state, free heap) for anyone debugging on the bench.


## How It Works — Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                        ESP32-S3 Board                           │
│                                                                 │
│  ┌──────────────┐    ┌──────────────────────────────────────┐   │
│  │  WiFi SoftAP │    │        NimBLE BLE Peripheral         │   │
│  │ 192.168.4.1  │    │  Advertises as HID Keyboard          │   │
│  │              │    │  Up to 9 concurrent connections      │   │
│  │  WebServer   │    │  Per-device HID notify (unicast)     │   │
│  │  REST API    │    │  OS fingerprint via MTU + addr type  │   │
│  │  Ducky Script│    │                                      │   │
│  └──────┬───────┘    └────────────┬─────────────────────────┘   │
│         │                         │                             │
│         │ HTTP                    │ BLE RF                      │
└─────────┼─────────────────────────┼─────────────────────────────┘
          │                         │
    ┌─────▼──────┐         ┌────────▼───────────────────────┐
    │  Browser   │         │  BLE Clients (HID Hosts)        │
    │ (phone/PC) │         │  Windows PC  Android  iOS/Mac   │
    │ Portal UI  │         │  Linux       (any BLE device)   │
    └────────────┘         └────────────────────────────────┘
```


## Hardware
- Board : ESP32-S3 Dev Module 
- BLE stack : NimBLE-Arduino (h2zero) 
- Flash layout : Default 4MB with SPIFFS
- Connectivity : BLE (peripheral, HID) + WiFi (SoftAP) simultaneously 

An ESP32-S3 is required specifically : the BLE bonding/CCCD storage pools
are raised past their stock size at compile time to support 9 concurrent
hosts, and that needs the extra RAM the S3 has over older ESP32 variants.


## Recommended
- Use those ESP32-S3 boards which has at least 8MB flash memory.
- Tested board : ESP32 S3 N16R8 (YD-ESP32-23)

## Getting started

### 1. Flash it with Arduino IDE
Install ESP32 board support
- Arduino IDE → Preferences → Additional Board URLs
- Paste following url and enter : https://espressif.github.io/arduino-esp32/package_esp32_index.json
- Tools → Board → Boards Manager → Install ESP32
```bash
Arduino IDE: Board = "ESP32S3 Dev Module"
Partition Scheme = "Default 4MB with spiffs"
USB CDC On Boot = Enable (critical for Serial Monitor logs)
Upload Speed : 921600
```
**Required Lybraries** 
- NimBLE-Arduino library (by h2zero)
- Arduino ESP32 by Espressif

Open the .ino in Arduino IDE
select your COM port and hit Upload

**Open Serial Monitor**
Tools → Serial Monitor (or Ctrl+Shift+M) and set baud rate as 115200 to see the serial logs

### 2. Connect to the dashboard

On boot, Dex BLE starts a WiFi access point:

```
SSID:     ESP32-HID-Hub      (configurable)
Password: hidcentral1        (change this — see Settings)
Portal:   http://192.168.4.1/
```

Join that network from your phone or laptop and open the portal URL.

### 3. Pair a device

Dex BLE advertises over BLE as `ESP32-HID-KB`. Pair it from any
device's Bluetooth settings like you would any keyboard. Once it connects
and subscribes, it shows up on the dashboard with an auto-detected label.

### 4. Run something

Pick a macro from the library, or drop your own Ducky Script into the
runner:

```
DELAY 500
GUI r
DELAY 300
STRING notepad
ENTER
```

Target one connected host from the dropdown, or leave it on **ALL** to broadcast.

## Serial Log Reference

All logs are prefixed with a tag for easy filtering. Output appears on both USB CDC (`Serial`) and UART0 GPIO43 (`Serial0`).

| Prefix | Section | What it logs |
|---|---|---|
| `[CFG]` | Config | NVS load/save events, values |
| `[SYS]` | System | Boot events, reboot |
| `[BLE]` | BLE stack | Connect, disconnect, auth, CCCD, advertising |
| `[HOSTS]` | Host registry | Add/remove/subscribe events |
| `[OSDET]` | OS detection | MTU readings, label assignment, retry |
| `[BLOCK]` | Blocklist | Block/unblock events, expiry |
| `[LABEL]` | Labels | NVS label save events |
| `[SCRIPT]` | Script engine | Task start/stop, per-line execution |
| `[WEB]` | Web API | Endpoint calls, parameters |
| `[HEARTBEAT]` | Loop | Every 10s: uptime, hosts, bonds, advertising, heap |

### Filtering examples (Arduino Serial Monitor search bar)
```
[OSDET]     ← only OS detection events
[BLE]       ← only BLE events
[HEARTBEAT] ← only periodic status
reason=     ← only disconnect events (shows reason code)
mtu=        ← only MTU-related logs
```


## Built-in Macro Library

35 macros organized by category. Load any macro by clicking its button in the portal — it populates the editor for review/edit before running.

### Diagnostics
| Macro | Action |
|---|---|
| ⌨ Test: Type Text | Types "BLE HID working!" - use to verify the connection |
| 🔬 Test: Ctrl+A | Sends Ctrl+A — tests modifier key handling |

### System (Windows)
| Macro | Action |
|---|---|
| 🔒 Lock Screen | `GUI l` |
| ⚡ Shutdown (Win) | `shutdown /s /t 0` via Run |
| 🔄 Restart (Win) | `shutdown /r /t 0` via Run |
| 💤 Sleep (Win) | Win+X → U → S |
| 🖥 Show Desktop | `GUI d` |
| 📊 Task Manager | Ctrl+Shift+Esc |
| ❌ Close Window | Alt+F4 |

### System (Linux)
| Macro | Action |
|---|---|
| ⚡ Shutdown (Linux) | Opens terminal → `sudo shutdown -h now` |
| 🔄 Restart (Linux) | Opens terminal → `sudo reboot` |

### WiFi (Windows)
| Macro | Variables | Action |
|---|---|---|
| 📶 WiFi On (Win) | - | Opens `ms-settings:network-wifi` |
| 🌐 WiFi Connect (Win) | `{{WIFI_SSID}}` | `netsh wlan connect name="SSID"` via CMD |
| 📡 WiFi Add+Connect (Win) | `{{WIFI_SSID}}` | PowerShell profile add + connect |

### WiFi (Linux)
| Macro | Variables | Action |
|---|---|---|
| 📶 WiFi On (Linux) | - | `nmcli radio wifi on` |
| 🌐 WiFi Connect (Linux) | `{{WIFI_SSID}}`, `{{WIFI_PASS}}` | `nmcli dev wifi connect "SSID" password "PASS"` |

### Applications
| Macro | Action |
|---|---|
| 📝 Notepad | Opens Notepad (Windows) |
| 🔢 Calculator | Opens Calculator (Windows) |
| 🖥 CMD | Opens Command Prompt |
| 🐧 Terminal (Linux) | Ctrl+Alt+T |
| 🌐 Open Browser | Opens browser to google.com |
| 📁 File Explorer | `GUI e` |

### Edit & Browser
| Macro | Keys |
|---|---|
| 📋 Copy | Ctrl+C |
| 📌 Paste | Ctrl+V |
| ✂ Cut | Ctrl+X |
| ↩ Undo | Ctrl+Z |
| 💾 Save | Ctrl+S |
| ☑ Select All | Ctrl+A |
| ➕ New Tab | Ctrl+T |
| ✖ Close Tab | Ctrl+W |
| 🔄 Refresh | F5 |
| ⛶ Fullscreen | F11 |
| 📷 PrintScreen | PrtSc |
| 📸 Snipping Tool | Win+Shift+S |
| 🔁 Alt+Tab | Alt+Tab |


## Ducky script examples
Visit Hak5 Ducky Script quick reference to learn ducky script.
https://documentation.hak5.org/hak5-usb-rubber-ducky/duckyscript-tm-quick-reference

Open CMD as Administrator Mode
```
WAIT 1000
GUI R
WAIT 1000
TYPE cmd
WAIT 1000
CTRL SHIFT ENTER
WAIT 1300
ALT Y
```

Create A New Folder
```
WAIT 1000
CTRL SHIFT N
WAIT 1200
TYPE hello
WAIT 1100
ENTER
```

Open notepad and type Hello World!
```
WAIT 1000
GUI R
WAIT 1000
TYPE notepad
WAIT 1000
ENTER
WAIT 1000
TYPE Hello World!
```
