# ESP32 Library API

A comprehensive REST API running on ESP32 microcontroller for managing a book library with an integrated web interface.

## Features

- **RESTful API**: Full CRUD operations for managing book records
- **SQLite Database**: Persistent storage using SQLite on SD card
- **Web Interface**: Built-in web interface for browsing and managing books
- **Authentication System**: Admin login with session-based authentication
- **Search Functionality**: Search books by title, author, ISBN, location, keywords, or synopsis
- **Backup System**: Database backup functionality with download capability
- **WiFi Connectivity**: Connects to your local network
- **mDNS Support**: Accessible via `library.local` hostname

## Hardware Requirements

- ESP32 development board
- SD card module for database storage
- WiFi network connection

## Setup

1. **Hardware Setup**:
   - Connect SD card module to ESP32
   - Ensure proper power supply for ESP32

2. **Configuration**:
   - Create a `config` file on the SD card with the following format:
     ```
     wifi_ssid=YOUR_WIFI_SSID
     wifi_password=YOUR_WIFI_PASSWORD
     admin_password=YOUR_ADMIN_PASSWORD
     proxy_host=YOUR_PROXY_HOST_IP
     ```
   - The `proxy_host` is used for admin authentication (only requests from this IP will have admin privileges)

3. **Software Dependencies**:
   - Arduino IDE with ESP32 board support
   - SQLite3 library for ESP32
   - ArduinoJson library
   - WiFi, WebServer, and SD libraries (included with ESP32 core)

## API Endpoints

- `GET /` - Main web interface
- `GET /books` - Retrieve books with pagination and search capabilities
- `POST /authenticate` - Admin login
- `GET /logout` - Admin logout
- `POST /add` - Add a new book (admin only)
- `POST /edit` - Update book information (admin only)
- `POST /delete` - Delete a book (admin only)
- `GET /backup` - Download database backup (admin only)
- `GET /img` - Serve images from SD card

## Web Interface

![ESP32 Library API Interface](../img/esp32_lib_1.jpg)

The web interface provides:

- Book browsing with pagination
- Search functionality across all fields
- Admin login/logout
- Add, edit, and delete books (admin only)
- Database backup download (admin only)
- Book cover images from Open Library

## Authentication

Admin users can perform write operations (add, edit, delete books, download backups). Authentication is session-based using cookies. Only requests from the configured `proxy_host` IP address can perform admin actions.

## Backup Script

The repository includes a Python backup script (`backup.py.template`) that can be used to programmatically download database backups from the ESP32.

## Database Schema

The SQLite database contains a `books` table with the following columns:
- `id` - Unique identifier
- `title` - Book title
- `author` - Book author(s)
- `isbn` - ISBN number
- `location` - Physical location of the book
- `keywords` - Associated keywords
- `synopsis` - Book synopsis/description

## Images

![ESP32 Library API Interface](../img/esp32_lib_2.jpg)

## Troubleshooting

- If the ESP32 doesn't connect to WiFi, verify the credentials in the config file
- If authentication fails, ensure the requesting IP matches the `proxy_host` in config
- If database operations fail, check SD card connections and permissions