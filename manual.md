# Genex Space Farming Kit — User Manual

Measure temperature, humidity, and soil moisture; control a water pump and **Red/Blue** grow LEDs via an offline web dashboard hosted by the device’s own Wi‑Fi AP.

---

## 1) Kit Contents

1. **ESP32**
2. **DHT11** (temperature & humidity)
3. **Soil moisture sensor** (analog capacitive probe)
4. **Wall adaptor** (power supply)
5. **Main control board** (carrier/driver PCB)
6. **LED strips – Red and Blue**
7. **Laser‑cut enclosure**
8. **Screws and standoffs**
9. **Net pots**
10. **Water reservoir and tube**
11. **Pump**

> Keep electronics dry. Use the enclosure and route tubing so that leaks cannot reach the PCB.

---

## 2) Device Wi‑Fi & Dashboard

* On boot, the kit starts a secured AP with unique credentials built from `uniqueID`:

```cpp
String AP_SSID = String("GenexFarmkit_") + uniqueID;
String AP_PASS = String("12345678") + uniqueID;  // password also includes ID
```

* Connect to the AP, then open \`\` to access the dashboard.

---

## 3) Firmware Location

* Source file: \`\`

> Flash with Arduino IDE (Board: *ESP32 Dev Module*). Serial Monitor @ **115200** shows logs and AP IP.

---

## 4) Default Pin Mapping

```cpp
#define DHTPIN   27      // DHT11 Temperature/Humidity sensor
#define DHTTYPE  DHT11
#define SOIL_PIN 34      // Soil moisture sensor (ADC)
#define RED_LED  14      // Red LED (PWM)
#define BLUE_LED 13      // Blue LED (PWM)
#define PUMP_PIN 4       // Pump driver pin
```

You may adjust pins in the firmware if you rewire the kit.

---

## 5) Quick Assembly

1. Mount the **main control board** into the **laser‑cut enclosure** using **screws and standoffs**.
2. Insert **net pots** into the **water reservoir** lid; route the **tube** from pump to the grow area.
3. Connect **DHT11** and **soil moisture** probe to the labeled headers.
4. Attach **Red/Blue LED strips** to the LED outputs (observe polarity).
5. Connect the **pump** to the PUMP OUT terminals (ensure flyback diode on the driver PCB).
6. Power the system with the **wall adaptor**.

---

## 6) Using the Dashboard

* **Readings:** Temperature (°C), Humidity (%), Soil Moisture (%)
* **Charts:** Local `<canvas>` trends (offline; no external libraries)
* **LED Controls:** Red/Blue brightness sliders (0–255) + **Auto**
* **Pump Control:** ON / OFF + **Auto** (moisture‑based, with hysteresis)
* **Sensor Status:** Badges show when a sensor is auto‑disabled after repeated faults.

---

## 7) Auto Logic (Defaults)

| Function      | Behavior                                       |
| ------------- | ---------------------------------------------- |
| Pump AUTO     | ON when moisture < **35%**, OFF when > **45%** |
| Red LED AUTO  | Brightness follows **temperature** (scaled)    |
| Blue LED AUTO | Brightness follows **humidity** (scaled)       |

If the corresponding sensor is disabled/unavailable, AUTO for that channel pauses.

---

## 8) HTTP API (Local)

Base URL: `http://192.168.4.1`

| Endpoint              | Method | Params                     | Description                                |                             |
| --------------------- | ------ | -------------------------- | ------------------------------------------ | --------------------------- |
| `/api/sensors`        | GET    | —                          | `{ temp, hum, moisture, dht_ok, soil_ok }` |                             |
| `/api/led/red`        | POST   | `v=0..255` **or** `auto=1` | Set Red LED brightness or enable AUTO      |                             |
| `/api/led/blue`       | POST   | `v=0..255` **or** `auto=1` | Set Blue LED brightness or enable AUTO     |                             |
| `/api/pump`           | POST   | \`state=on                 | off`**or**`auto=1\`                        | Control pump or enable AUTO |
| `/api/sensor/disable` | POST   | `dht=1` or `soil=1`        | Manually disable sensor                    |                             |
| `/api/sensor/enable`  | POST   | `dht=1` or `soil=1`        | Re‑enable sensor                           |                             |

---

## 9) Suggested Add‑On Sensors (Optional)

* **Light**: BH1750 (I²C) or LDR + resistor (analog)
* **Air quality**: MQ‑135 (analog), SGP30 (I²C)
* **Environment**: BMP280/BME280 (I²C) for pressure/temperature/humidity

Use the board’s **ADC** and **I²C (SDA/SCL)** headers for expansion.

---

## 10) Soil Moisture Calibration

1. Measure raw ADC values with the probe **dry** and **fully wet**.
2. In firmware, tune the mapping constants:

```cpp
const float DRY = 3000.0;   // set to your dry reading
const float WET = 1200.0;   // set to your wet reading
```

3. Rebuild and verify that moisture (%) behaves realistically.

---

## 11) Safety & Best Practices

* Keep electronics away from water; secure tubing to prevent leaks.
* Ensure **common ground** when using a separate pump supply.
* Do not block ventilation for LEDs/regulators.
* Always connect LED/pump terminals with correct polarity.

---

## 12) Troubleshooting

| Symptom                   | Likely Cause                  | Fix                                                    |
| ------------------------- | ----------------------------- | ------------------------------------------------------ |
| No AP visible             | Power or flash issue          | Check wall adaptor/USB; reflash; view serial logs      |
| Readings are `--`         | Sensor not wired / warming up | Check headers; wait a few cycles; see `dht_ok/soil_ok` |
| Pump never starts in AUTO | Thresholds not met            | Lower `MOISTURE_ON` or recalibrate DRY/WET             |
| LEDs behave inverted      | Driver topology               | Invert PWM logic or wiring as appropriate              |

---

## 13) Contact

* **Support:** [info@genex.space](mailto:info@genex.space)
* **Website:** [https://genex.space](https://genex.space)
