# IoT Sensor Logger

This project reads temperature, humidity, and light data from an ESP32-based IoT device, logs the data to a Supabase database, and sends notifications via ntfy.

## Table of Contents
- [Prerequisites](#prerequisites)
- [Project Structure](#project-structure)
- [Setup](#setup)
  - [Clone Repository](#clone-repository)
  - [Environment Variables](#environment-variables)
  - [Supabase Functions](#supabase-functions)
  - [ESP32 Firmware](#esp32-firmware)
- [Configuration](#configuration)
- [Testing & Debugging](#testing--debugging)
- [Gitignore](#gitignore)
- [License](#license)

## Prerequisites

- **ESP32** development board
- **PlatformIO** (via [PlatformIO IDE](https://platformio.org/) or CLI)
- **DHT22** temperature/humidity sensor (Zone 2)
- **MAX44009** light sensor (Zone 1)
- **Supabase** account and project
- **Supabase CLI** (for deploying Edge Functions)
- **Deno** (for running Supabase Edge Functions)
- **ntfy.sh** account or access to an ntfy topic for notifications

## Project Structure

```
└── iot/
    ├── src/
    │   └── main.cpp             # ESP32 firmware source code
    ├── supabase/
    │   ├── config.toml          # Supabase project settings (ignored)
    │   └── functions/
    │       └── log-sensor-data/
    │           └── index.ts     # Deno Supabase Edge Function to log and alert
    ├── .gitignore
    └── README.md
```

## Setup

### Clone Repository

```bash
git clone <YOUR_REPO_URL>
cd iot
```

### Environment Variables

Create a `.env` file in the project root (this file is ignored by Git):

```ini
# Supabase
MY_SUPABASE_URL="https://<YOUR_PROJECT_REF>.supabase.co"
MY_SUPABASE_SERVICE_KEY="<YOUR_SERVICE_ROLE_KEY>"

# ntfy notifications
NTFY_TOPIC_URL="https://ntfy.sh/<YOUR_TOPIC>"
NTFY_ACCESS_TOKEN="<YOUR_NTFY_TOKEN>"
SEND_NOTIFICATIONS="true"
```

> **Note:** Never commit `.env` to version control. All sensitive keys are loaded from environment variables.

Create `src/credentials.h` for your Wi-Fi and Supabase keys (also ignored by Git):

```c
#define WIFI_SSID     "<YOUR_WIFI_SSID>"
#define WIFI_PASSWORD "<YOUR_WIFI_PASSWORD>"
#define SUPABASE_URL  "<YOUR_SUPABASE_URL>"
#define SUPABASE_ANON_KEY "<YOUR_SUPABASE_ANON_KEY>"
```

### Supabase Functions

1. Install and login:
   ```bash
   supabase login
   ```
2. Deploy the Edge Function:
   ```bash
   supabase functions deploy log-sensor-data --project-ref <YOUR_PROJECT_REF>
   ```
3. Ensure the function has access to environment variables by updating `config.toml` or setting them in your Supabase dashboard.

### ESP32 Firmware

1. Install PlatformIO if not already installed:
   ```bash
   pip install platformio
   ```
2. Build and upload to the ESP32:
   ```bash
   platformio run --target upload
   ```

## Configuration

- **Sensor Pins & Thresholds:** Adjust pins and alert thresholds in `src/main.cpp`.
- **Database Schema:** Ensure your Supabase database has a table `sensor_logs` with columns matching:
  - `z1_temp`, `z1_lux`, `z1_alert`
  - `z2_temp`, `z2_humidity`, `z2_alert`
  - `fan_on`

## Testing & Debugging

- **Serial Monitor:** Use PlatformIO Serial Monitor to view logs:
  ```bash
  platformio device monitor
  ```
- **Supabase Logs:** View function invocations and errors in the Supabase dashboard.

## Gitignore

This project's `.gitignore` excludes local build artefacts, environment files, and secret headers. See [`.gitignore`](./.gitignore) for details.

## License

This project is licensed under the MIT License. See the `LICENSE` file for details. 