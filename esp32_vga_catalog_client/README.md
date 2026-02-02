# ESP32 VGA Catalog Client

A rich client application running on ESP32 with VGA output using the TTGO VGA shield (v1.4), providing an interactive library catalog interface with keyboard navigation.

## Features

- **VGA Output**: High-resolution display using FabGL library with 640x200 resolution
- **Keyboard Navigation**: Full keyboard control for browsing and searching books
- **Book Browsing**: Paginated display of library books with title, author, and location
- **Search Functionality**: Search books by title, author, ISBN, keywords, or synopsis
- **Book Details**: Detailed view of book information including synopsis
- **Printing Support**: Network printing of book details to a connected printer
- **Responsive UI**: Clean, color-coded interface optimized for VGA display

## Hardware Requirements

- ESP32 development board
- TTGO VGA Shield v1.4
- PS/2 Keyboard for input
- Network connectivity for API communication
- Optional: Network printer for printing book details

## Setup

1. **Hardware Setup**:
   - Connect the TTGO VGA Shield to your ESP32
   - Connect a PS/2 keyboard to the shield
   - Ensure proper power supply for ESP32 and VGA display

2. **Configuration**:
   - Update the WiFi credentials in the code:
     ```cpp
     const char* ssid = "YOUR_WIFI_SSID";
     const char* password = "YOUR_WIFI_PASSWORD";
     ```
   - Configure your API endpoint:
     ```cpp
     const char* apiUrl = "http://YOUR_API_IP_ADDRESS";
     ```
   - (Optional) Configure printer settings:
     ```cpp
     const char* printerIP = "YOUR_PRINTER_IP";
     const uint16_t printerPort = 9100;
     ```

3. **Software Dependencies**:
   - Arduino IDE with ESP32 board support
   - FabGL library (version 2.0.8 or compatible)
   - WiFi library (included with ESP32 core)
   - ArduinoJson library

## Controls

- `P`/`N` - Navigate to Previous/Next page
- `I` - Enter search mode to input search terms
- `F` - Select search field (title, author, ISBN, keywords, synopsis)
- `S` - Execute search with current query and field
- `R` - Reset search and return to full catalog
- `H` - Show help dialog with all keyboard commands
- `1-8` - View details of books 1-8 displayed on current page
- `Enter` - View details of currently highlighted book
- `↑`/`↓` - Navigate between books in the list
- `PgUp`/`PgDn` - Scroll through long book descriptions
- `ESC` - Return to main view or exit dialogs
- `P` (in details view) - Print book details to network printer

## User Interface

![ESP32 VGA Catalog Client](../img/lib_client_1.jpg)

The interface features:

- Header with application title
- Search controls with input field and field selector
- Table view showing books with title, author, and location
- Pagination controls
- Color-coded elements for better readability
- Highlighted selection for current book

## Book Details View

![ESP32 VGA Catalog Client Details](../img/lib_client_2.jpg)

When viewing book details, you can:

- See complete book information (title, author, ISBN, location)
- Read the full synopsis with pagination for long texts
- Print book details to a network printer
- Navigate through multiple pages of content

## Printing Functionality

The client supports sending book details to a network printer using raw TCP/IP communication on port 9100. The printed output includes:

- Title
- Author
- ISBN
- Location
- Keywords
- Full synopsis

## Troubleshooting

- If VGA output doesn't work, verify your TTGO VGA shield connections and resolution settings
- If WiFi connection fails, check your credentials and network availability
- If API communication fails, verify the API endpoint URL and network connectivity
- If keyboard input doesn't register, check PS/2 keyboard connections and FabGL configuration