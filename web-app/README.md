# EPD47 Dashboard Web App

A Next.js web application that emulates the EPD47 e-paper display layout, integrates with Home Assistant for todos/calendars, and supports OTA updates to the ESP32 device.

## Overview

This web application provides a browser-based preview of what your EPD47 e-paper display shows, allowing you to:
- View your dashboard layout before it appears on the physical device
- Configure which Home Assistant entities to display
- Upload firmware updates to your ESP32 device over-the-air (OTA)
- Self-host on your own server (e.g., Proxmox)

## Features

- **Dashboard Preview**: Emulates the 960x540 e-paper display layout with real-time updates
- **Home Assistant Integration**: Fetches weather, todos, calendar events, and quotes from your HA instance
- **Entity Configuration**: Select which Home Assistant entities to display via a user-friendly settings page
- **OTA Updates**: Upload firmware updates to the ESP32 device over-the-air without physical access
- **Self-Hostable**: Docker container ready for Proxmox or any Docker-compatible deployment
- **Low-Overhead Updates**: Dashboard refreshes only when needed (weather/hourly, date at midnight, clock/minute, quotes every 6 hours)

## Prerequisites

- Node.js 20+ or Docker
- Home Assistant instance with REST API access
- Long-lived access token from Home Assistant
- ESP32 device with OTA enabled

## Installation

### Local Development

1. Clone the repository and navigate to the web-app directory:
```bash
cd web-app
```

2. Install dependencies:
```bash
npm install
```

3. Copy `.env.example` to `.env.local` and configure:
```bash
cp .env.example .env.local
```

4. Run the development server:
```bash
npm run dev
```

5. Open [http://localhost:3000](http://localhost:3000)

### Docker Deployment

1. Create a `.env` file (or set environment variables):
```bash
HA_HOST=your_ha_host_or_ip
HA_PORT=8123
HA_TOKEN=your_long_lived_access_token
OTA_DEVICE_IP=your_esp32_device_ip
OTA_PASSWORD=your_ota_password
```

2. Build and run with Docker Compose:
```bash
docker-compose up -d
```

3. Or build manually:
```bash
docker build -t epd47-dashboard .
docker run -p 3000:3000 \
  -e HA_HOST=your_ha_host_or_ip \
  -e HA_PORT=8123 \
  -e HA_TOKEN=your_long_lived_access_token \
  -e OTA_DEVICE_IP=your_esp32_device_ip \
  -e OTA_PASSWORD=your_ota_password \
  epd47-dashboard
```

**Important**: Never commit your `.env` file or expose credentials in your code!

## Configuration

### Getting a Home Assistant Long-Lived Access Token

1. Open Home Assistant in your browser
2. Click on your profile (bottom left)
3. Scroll down to "Long-lived access tokens"
4. Click "Create Token"
5. Give it a name (e.g., "EPD47 Dashboard")
6. Copy the token immediately (you won't be able to see it again)

### Initial Setup

1. Navigate to the Settings page (`/settings`)
2. Enter your Home Assistant configuration:
   - **Host/IP address**: Your Home Assistant instance IP (e.g., `192.168.1.100` or `homeassistant.local`)
   - **Port**: Default is `8123`
   - **Long-lived access token**: Paste the token you created above
3. Wait for entities to load (dropdowns will populate automatically)
4. Select entities:
   - **Weather entity**: Choose from available weather entities
   - **Quote sensor entity**: Choose a sensor that contains quotes (e.g., `sensor.quote_of_the_day`)
   - **Todo entities**: Select one or more todo entities (optional)
   - **Calendar entities**: Select one or more calendar entities (optional)
5. Configure OTA settings:
   - **Device IP address**: Your ESP32 device's IP address on your network
   - **OTA password**: Must match the password set in your ESP32 firmware (`ArduinoOTA.setPassword()`)

**Note**: Configuration is saved in browser localStorage. Each browser/device needs to be configured separately.

## Usage

### Dashboard

The main dashboard (`/`) displays:
- Clock (updates every minute)
- Weather information
- Current date
- Daily quote (rotates every 6 hours)
- Todo list (items due today)
- Upcoming calendar events
- Mini calendar (current week)

### OTA Updates

1. Navigate to the OTA page (`/ota`)
2. Either select a firmware `.bin` file OR leave it empty to push the bundled firmware from the server
3. Click "Update Now"
4. Wait for the upload to complete (progress bar shown)
5. Device will restart automatically

Bundled firmware: set `DEFAULT_FIRMWARE_PATH=/absolute/path/to/your.bin` in the environment where the Next.js server runs. If this is not set, upload a `.bin` file manually from the OTA page.

Build & Upload: set `ENABLE_PIO_BUILD_UPLOAD=1` and `PIO_PROJECT_ROOT=/absolute/path/to/LilyGo-EPD47` if you want the web app to run PlatformIO directly from the OTA page. You can optionally override `PIO_CMD` and `PIO_ENV`.

**Note**: Ensure the device is powered on and connected to WiFi before uploading.

## API Routes

- `/api/ha/weather` - Fetch weather data
- `/api/ha/todos` - Fetch todo items
- `/api/ha/calendars` - Fetch calendar events
- `/api/ha/quotes` - Fetch quotes
- `/api/ota/update` - Upload firmware via OTA
- `/api/health` - Health check endpoint

## Project Structure

```
web-app/
├── app/                    # Next.js app directory
│   ├── api/               # API routes
│   ├── settings/          # Settings page
│   ├── ota/               # OTA update page
│   └── page.tsx           # Main dashboard
├── components/            # React components
│   ├── Dashboard.tsx      # Main dashboard component
│   ├── Clock.tsx          # Clock display
│   ├── Weather.tsx        # Weather section
│   ├── TodoList.tsx       # Todo list
│   ├── CalendarEvents.tsx # Calendar events
│   ├── MiniCalendar.tsx   # Mini calendar
│   └── Quote.tsx          # Quote display
├── lib/                   # Utilities
│   ├── ha-client.ts       # Home Assistant client
│   └── types.ts           # TypeScript types
└── public/                # Static assets
```

## Development

```bash
# Install dependencies
npm install

# Run development server
npm run dev

# Build for production
npm run build

# Start production server
npm start

# Lint code
npm run lint
```

## Environment Variables

These are optional and can be set as defaults. Configuration via the Settings page takes precedence.

- `HA_HOST` - Home Assistant host/IP (default fallback)
- `HA_PORT` - Home Assistant port (default: 8123)
- `HA_TOKEN` - Home Assistant long-lived access token (default fallback)
- `OTA_DEVICE_IP` - ESP32 device IP address (default fallback)
- `OTA_PASSWORD` - OTA password (must match device configuration)
- `DEFAULT_FIRMWARE_PATH` - Optional absolute path to a bundled firmware `.bin`
- `ENABLE_PIO_BUILD_UPLOAD` - Set to `1` to enable server-side PlatformIO Build & Upload
- `PIO_PROJECT_ROOT` - Optional absolute path to the PlatformIO project root for Build & Upload
- `PIO_CMD` - Optional PlatformIO command path (default: `pio`)
- `PIO_ENV` - Optional PlatformIO environment (default: `T5-ePaper-S3-OTA`)

**Security Note**: Never commit `.env` files or expose tokens in your code. Use environment variables or the Settings page for configuration.

## Troubleshooting

### Cannot fetch entities
- Verify Home Assistant host and port are correct
- Check that the access token is valid and hasn't expired
- Ensure Home Assistant is accessible from the web app server (not just your browser)
- Check browser console (F12) for CORS or network errors
- Verify your Home Assistant instance allows REST API access

### Entity dropdowns are empty
- Make sure you've entered both Host and Token in Settings
- Wait a few seconds after entering credentials (entities load automatically)
- Check the error message at the top of the Settings page
- Verify your token has proper permissions in Home Assistant

### OTA update fails
- Verify device IP address is correct (check your router's DHCP table)
- Check that OTA password matches device configuration (`ArduinoOTA.setPassword()`)
- Ensure device is powered on and connected to WiFi
- Verify device is on the same network as the web app server
- Check device serial output for error messages
- Try uploading via USB first to ensure the firmware file is valid

### Dashboard not updating
- Check browser console (F12) for errors
- Verify API routes are accessible (`/api/health` should return `{"status":"ok"}`)
- Ensure Home Assistant entities exist and are accessible
- Check that selected entities match your Home Assistant setup
- Try clicking "Reload Config" button on the dashboard page

### CORS errors
- The web app uses server-side API routes to avoid CORS issues
- If you see CORS errors, ensure you're accessing the web app through the Next.js server, not directly from Home Assistant

## Security Considerations

- **Never commit credentials**: All `.env` files are gitignored
- **Use HTTPS in production**: For production deployments, use a reverse proxy (nginx, Traefik) with SSL
- **Token security**: Long-lived tokens have full access to your Home Assistant instance. Keep them secure.
- **Network security**: Consider firewall rules to limit access to the web app
- **OTA password**: Use a strong password for OTA updates to prevent unauthorized firmware uploads

## Contributing

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add some amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## License

ISC
