# 🧠 Project A7S-chan

> **A7S 做腦，Stack-chan 做身 — 有視覺、有對話、有表情、有 IR 控制嘅智能機械人**

---

## 一、系統架構概覽

```
┌─────────────────────────────────────────────────────────────────┐
│  A7S (大腦) — Radxa Cubie A7S, Allwinner A733                    │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │  Main Controller (Python, asyncio)                         │  │
│  │                                                             │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │  │
│  │  │ NPU Vision│  │  Agent   │  │  Voice   │  │ Behavior │   │  │
│  │  │ @30fps   │  │ (Pydantic│  │ Pipeline │  │  State   │   │  │
│  │  │ face/obj │  │  AI)     │  │ STT→LLM  │  │ Machine  │   │  │
│  │  └────┬─────┘  └────┬─────┘  │ →TTS    │  └────┬─────┘   │  │
│  │       │              │        └─────────┘       │           │  │
│  │       └──────┬───────┴────────────┬─────────────┘           │  │
│  │              │                    │                          │  │
│  │        Shared State         Behavior Queue                  │  │
│  │     (face_x, face_y,      (speak, move, emote,              │  │
│  │      objects, mode)         ir_blast, show)                 │  │
│  └────────────────────────────────────────────────────────────┘  │
│                           │                                       │
│                    WiFi UDP/WS                                    │
│                           │                                       │
├─────────────────────────────────────────────────────────────────┤
│  Stack-chan (身體) — M5Stack CoreS3 + 底座                      │
│                                                                  │
│  Custom Firmware (ESP-IDF)                                       │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │  WiFi Client → A7S                                          │  │
│  │  ├─ 接收指令: head(pan,tilt), face(expression),             │  │
│  │  │  led(color), speak(text), ir(code), screen(show)        │  │
│  │  ├─ 發送狀態: sensor data, battery, touch events            │  │
│  │  └─ 發送音頻: PDM mic → 16kHz PCM → UDP to A7S             │  │
│  │                                                             │  │
│  │  Hardware Control (直接)                                     │  │
│  │  ├─ 2× Servo (pan 360°, tilt 90°)                          │  │
│  │  ├─ Screen: 320×240 IPS, 表情系統                           │  │
│  │  ├─ 12× RGB LEDs                                            │  │
│  │  ├─ IR TX/RX (冷氣/電視遙控)                                 │  │
│  │  ├─ 1W Speaker (TTS 輸出)                                   │  │
│  │  ├─ Dual Mic (語音輸入)                                      │  │
│  │  └─ IMU + Touch Sensor                                      │  │
│  └────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

---

## 二、通訊協議 — A7S ↔ Stack-chan

### 方式：WiFi UDP（優先）+ TCP/WebSocket（fallback）

Stack-chan 開機後自動 connect A7S 嘅 WiFi hotspot（或同一 LAN），然後維持 UDP 連接。

### 指令格式 (JSON over UDP)

```
A7S → Stack-chan (控制指令)：
────────────────────────────────────
{ "cmd": "head",   "pan": 0..360, "tilt": 5..85 }
{ "cmd": "face",   "expression": "happy|sad|angry|surprise|blink|idle|talk|sleep" }
{ "cmd": "led",    "r": 255, "g": 0, "b": 0, "pattern": "solid|blink|wave" }
{ "cmd": "speak",  "text": "Hello!" }
{ "cmd": "ir",     "protocol": "nec", "address": 0x00, "command": 0x10 }
{ "cmd": "screen", "mode": "weather", "data": { "temp": 28, "humidity": 70 } }
{ "cmd": "emote",  "animation": "dance|nod|shake|wave|think" }

Stack-chan → A7S (狀態回報)：
────────────────────────────────────
{ "status": "ok", "battery": 85 }
{ "event": "touch", "zone": 1|2|3 }
{ "event": "imu",  "accel": [0.1, 0.0, -1.0], "gyro": [0,0,0] }
```

### 音頻串流 (UDP raw PCM)
- Mic: 16kHz, 16-bit, mono → UDP to A7S port 9001
- Speaker: 從 A7S 接收 PCM → I2S DAC → 1W Speaker

---

## 三、Custom Firmware 開發

### 開發環境

| Item | 版本 / 工具 |
|------|-------------|
| **Framework** | ESP-IDF v5.5.4 |
| **Language** | C++17 |
| **Board** | M5Stack CoreS3 (ESP32-S3) |
| **Build** | `idf.py build` |
| **Flash** | `idf.py flash` via USB-C |
| **Monitor** | `idf.py monitor` |

### Firmware 結構

```
firmware/
├── main/
│   ├── main.cpp              ← Entry point: WiFi connect + event loop
│   ├── udp_client.cpp/h      ← UDP 通訊協議
│   ├── face_renderer.cpp/h   ← 表情渲染系統
│   ├── servo_control.cpp/h   ← 摩打控制
│   ├── ir_control.cpp/h      ← IR 發射
│   ├── audio_pipeline.cpp/h  ← 音頻 I/O
│   ├── led_control.cpp/h     ← RGB LED
│   └── assets/               ← 表情圖像資源
├── CMakeLists.txt
├── sdkconfig.defaults
└── partitions.csv
```

### Flash 步驟

```bash
# 1. Install ESP-IDF v5.5.4
git clone -b v5.5.4 https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32s3 && . ./export.sh

# 2. Build firmware
cd ~/stackchan-firmware
idf.py set-target esp32s3
idf.py build

# 3. Flash (USB-C 連接)
idf.py flash

# 4. Monitor serial output
idf.py monitor
```

---

## 四、完整表情包 (Expression Pack)

表情系統係關鍵互動層。Stack-chan 用 320×240 IPS 屏幕顯示。

### 表情列表

```
┌──────────────────────────────────────────────────────────────────┐
│  ID          │ 顯示                          │ 觸發場景            │
├──────────────┼────────────────────────────────┼────────────────────┤
│  idle        │ 👀 眨眼中，微微左右望          │ 默認狀態            │
│  happy       │ 😊 彎眼笑，有時跳跳             │ 被讚/成功           │
│  sad         │ 😢 垂眼，嘴向下                 │ 拒絕/失敗           │
│  angry       │ 😠 眉上揚，嘴變形               │ 指令無效/被煩        │
│  surprise    │ 😮 O 嘴，瞪大眼                 │ 突發事件/新發現      │
│  blink       ｜ 👁️ 快速眨眼一次               ｜ 隨機/定時           │
│  talk        │ 🗣️ 嘴型跟 TTS 同步             │ 正在說話            │
│  listen      │ 👂 側耳狀，微微傾頭             │ 等待語音輸入         │
│  think       │ 🤔 眼向上，摸下巴動作           │ 處理中 / LLM 思考    │
│  sleep       │ 😴 Zzz 動畫，眼半合            │ 閒置 > 30秒         │
│  wake        │ ✨ 眼發光，微笑醒來             │ 被喚醒              │
│  confused    │ 😕 歪頭，問號                   │ 聽唔明指令          │
│  excited     │ 🤩 星星眼，震動                 │ 特別任務/開心       │
│  dance       │ 🕺 配合摩打左右搖 + LED 閃     │ 播放音樂/開心       │
│  love        │ ❤️ 心心眼，面紅                 │ 被表白/讚美         │
│  wink        │ 😉 單眼                        │ 俏皮回應            │
│  sweat       │ 😅 滴汗                        │ 壓力/尷尬           │
│  crying      │ 😭 喊                          │ 太耐冇人理          │
│  custom      │ 🎨 動態生成表情                │ Agent 自訂          │
└──────────────────────────────────────────────────────────────────┘
```

### 表情渲染方式

每款表情係由以下層次組合：
```
[Background] → [Face base] → [Eyes] → [Eyebrows] → [Mouth] → [Blush/Deco]
```

每個部分都係 SVG-like 向量繪圖 (用 LovyanGFX library)，可以動態生成：
- 眼: 圓形/半圓/弧形，瞳孔位置動態
- 嘴: 弧線/圓形/鋸齒，開口大小
- 眉: 角度/高度
- 特效:  blush (面紅)、teardrop (眼淚)、exclamation (感嘆號)

### 表情轉換邏輯

```
表情轉換唔可以硬切 — 要用 tween animation：
  current → [tween 100ms] → next

例如：
  happy → tween(0.3s) → idle
  idle → tween(0.1s) → surprise → tween(0.5s) → happy
```

---

## 五、A7S Agent 軟體架構

### Python Stack

```
pip install pydantic-ai httpx pillow numpy
```

### 主程式結構

```
a7s_chan/
├── main.py                  ← asyncio entry point
├── agent.py                 ← PydanticAI agent + tool registry
├── vision.py                ← NPU inference wrapper (VIPLite)
├── voice.py                 ← STT (Grok xAI) + TTS pipeline
├── stackchan_proto.py       ← UDP protocol to Stack-chan
├── behavior.py              ← State machine (idle/track/converse/auto)
├── tools/
│   ├── ir_remote.py         ← IR code database (冷氣/電視)
│   ├── weather.py           ← 天氣 API
│   └── screen_display.py    ← 顯示內容到 Stack-chan 屏幕
├── expressions/
│   └── pack.py              ← 表情參數定義
└── config.yaml              ← API keys, WiFi, 行為參數
```

### Agent Tools (PydanticAI)

```python
@agent.tool
async def control_head(pan: int, tilt: int):
    """控制 Stack-chan 頭部角度"""
    await send_udp({"cmd": "head", "pan": pan, "tilt": tilt})

@agent.tool
async def set_expression(expr: str):
    """設定面部表情"""
    await send_udp({"cmd": "face", "expression": expr})

@agent.tool
async def ir_blast(device: str, action: str):
    """發射紅外線控制家電（冷氣/電視）"""
    code = IR_DATABASE[device][action]
    await send_udp({"cmd": "ir", **code})

@agent.tool
async def get_weather(city: str = "Hong Kong"):
    """查詢天氣"""
    data = await call_weather_api(city)
    await send_udp({"cmd": "screen", "mode": "weather", "data": data})
    return f"{city}: {data['temp']}°C, {data['condition']}"

@agent.tool
async def speak(text: str):
    """TTS 輸出 + 口型同步"""
    audio = await tts(text)
    await send_audio(audio)
    await set_expression("talk")
```

### Behavior State Machine

```
                    ┌──────────┐
                    │  SLEEP   │ ← 閒置 30s
                    └────┬─────┘
                         │ wake word / touch
                    ┌────▼─────┐
         ┌─────────┤   IDLE   ├──────────┐
         │         │  周圍望   │          │
         │         └────┬─────┘          │
    face detected  ┌────▼─────┐     voice cmd
         │         │ TRACKING │──────────┤
         ├────────►│ 追人面   │          │
         │         └────┬─────┘          │
         │         ┌────▼─────┐          ▼
         │         │ CONVERSE │     ┌──────────┐
         │         │  對話中  │     │  EXECUTE │
         │         └────┬─────┘     │ IR/顯示  │
         │         ┌────▼─────┐     └────┬─────┘
         │         │AUTONOMOUS│          │
         └────────►│ 自由活動 │◄─────────┘
                   └──────────┘
```

---

## 六、第一階段實作順序

### Phase 1: 基礎通訊 (Week 1)
- [ ] Custom firmware 連 WiFi + UDP 接收
- [ ] A7S UDP server 收發指令
- [ ] 基本 servo 控制（head pan/tilt）

### Phase 2: 表情系統 (Week 1-2)
- [ ] 向量繪圖表情引擎（LovyanGFX）
- [ ] 12+ 款基本表情
- [ ] Tween animation 轉場
- [ ] 由 UDP 指令切換表情

### Phase 3: 視覺 + NPU (Week 2)
- [ ] VIPLite NPU 人面檢測
- [ ] 人面追蹤 → servo 跟隨
- [ ] YOLO 物件辨識
- [ ] 30fps real-time loop

### Phase 4: Agent + 對話 (Week 2-3)
- [ ] PydanticAI agent setup
- [ ] STT + TTS pipeline
- [ ] IR 遙控 database
- [ ] 天氣 + 顯示功能
- [ ] Behavior state machine

### Phase 5: 整合 + 打磨 (Week 3)
- [ ] 完整語音互動流程
- [ ] 表情 + 動作同步
- [ ] 離線 fallback
- [ ] 表情包擴充

---

## 七、硬件連接圖

```
┌────────────────────────────────────────────┐
│  A7S (51×51mm)                             │
│                                             │
│  GPIO Header (30-pin):                      │
│  ├─ UART TX/RX → ESP32 UART (備用通訊)      │
│  ├─ I2C SDA/SCL → sensor expansion          │
│  ├─ 5V/GND → power                           │
│  │                                           │
│  MIPI CSI (4-lane):                          │
│  └─ 高質 Camera (optional upgrade)           │
│                                             │
│  WiFi: AP mode — Stack-chan 直接 connect     │
└────────────────────────────────────────────┘
           │ WiFi UDP
           ▼
┌────────────────────────────────────────────┐
│  Stack-chan (M5Stack CoreS3 + 底座)        │
│                                             │
│  USB-C → Power 5V                           │
│  Servo X → Pan (360° continuous)            │
│  Servo Y → Tilt (5°-85°)                    │
│  IR TX/RX → 冷氣/電視                       │
│  320×240 IPS → 表情 + 資訊                  │
│  雙 Mic → 語音輸入                          │
│  1W Speaker → TTS 輸出                      │
│  12× RGB LED → 氣氛燈                      │
└────────────────────────────────────────────┘
```

---

## 八、表情系統技術細節

### 渲染 Engine: LovyanGFX

LovyanGFX 係 ESP32 上最快嘅圖形 library，支援：
- 硬件加速 sprite
- 雙 buffer 無閃爍
- 抗鋸齒圓形/弧線

### 表情參數化

每款表情係一組參數，唔係 bitmap：

```cpp
struct Expression {
    // 眼
    float eye_open;        // 0.0~1.0 睜眼程度
    float eye_height;      // 瞳孔高度
    float eye_width;       // 0.5~2.0
    float pupil_x, pupil_y; // 瞳孔位置
    
    // 眉
    float brow_angle;      // -30~30 度
    float brow_height;     // -10~10
    
    // 嘴
    float mouth_open;      // 0.0~1.0
    float mouth_curve;     // -1.0~1.0 (frown to smile)
    
    // 特效
    uint8_t blush;         // 0~255 面紅
    bool tear;             // 眼淚
    bool exclamation;      // 感嘆號
    bool heart_eyes;       // 心心眼
};
```

好處：
- 可以動態 interpolate 兩個表情之間嘅 tween
- 可以實時跟 TTS 同步 mouth open
- 可以 Agent 自訂表情

---

## 九、Redpill 路徑

呢個企劃可以由兩個方向開始：

**A) 由 Custom Firmware 開始（推薦）：**
先搞掂 Stack-chan 嘅 custom firmware（WiFi + UDP + 表情），令佢可以脫離原廠 cloud，然後逐層加 A7S 功能上去。

**B) 由 A7S Agent 開始：**
先喺 A7S 寫好 agent + vision + voice，然後先搞 firmware 落 Stack-chan。

我建議 **A** — firmware 係基礎，搞掂咗通訊先，後面全部可以喺 A7S 用 Python 快速 iterate。
