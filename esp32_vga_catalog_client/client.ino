#include "fabgl.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <WiFiClient.h>

// Color definitions for 8-color mode
#define COLOR_BLACK   RGB888(0x00, 0x00, 0x00)  // Black
#define COLOR_RED     RGB888(0x80, 0x00, 0x00)  // Red
#define COLOR_GREEN   RGB888(0x00, 0x80, 0x00)  // Green
#define COLOR_YELLOW  RGB888(0x80, 0x80, 0x00)  // Yellow
#define COLOR_BLUE    RGB888(0x00, 0x00, 0x80)  // Blue
#define COLOR_MAGENTA RGB888(0x80, 0x00, 0x80)  // Magenta
#define COLOR_CYAN    RGB888(0x00, 0x80, 0x80)  // Cyan
#define COLOR_WHITE   RGB888(0x80, 0x80, 0x80)  // White (light gray in 8-color mode)

// WiFi credentials - replace with your network info
const char* ssid = "";
const char* password = "";
const char* printerIP = "";  // Replace with actual printer IP
const uint16_t printerPort = 9100;        // Standard printer port


// API configuration
const char* apiUrl = "http://";

// FabGL setup
fabgl::VGA4Controller VGAController;
fabgl::Canvas canvas(&VGAController);
fabgl::PS2Controller PS2Controller;
fabgl::Keyboard *keyboard;


bool inputMode = false;
bool showSearchDropdown = false;
String searchFields[] = {"title", "author", "isbn", "keywords", "synopsis"};
int selectedFieldIndex = 0;

// Data structures
const int MAX_BOOKS_PER_PAGE = 8; // Maximum number of books per page

struct Book {
  int id;
  String title;
  String author;
  String isbn;
  String location;
  String keywords;
  String synopsis;
  String coverUrl;
};

Book books[MAX_BOOKS_PER_PAGE]; // Store up to MAX_BOOKS_PER_PAGE books per page
int totalBooks = 0;
int currentPage = 1;
int totalPages = 0;
String searchQuery = "";
String searchField = "title"; // Default search field

// UI elements
bool showDetailsDialog = false;
int selectedBookIndex = -1;
int detailsPagination = 0; // Pagination index for details dialog
bool helpDialog = false; // Flag for help dialog
int totalDetailsPages = 1; // Total number of pages in the details dialog
int scrollOffset = 0; // For scrolling through the book list

// Forward declarations
String truncateText(String text, int maxLength);
void drawUI();
void fetchBooks(int page, String search = "", String field = "");
void parseJson(String jsonString);
void drawTable();
void drawPaginationControls();
void drawSearchControls();
void drawDetailsDialog();
void handleInput();
void showLoadingDialog();
void showHelpDialog();
void showDialog(String message);
void showBookDetails(int bookIndex);
// void drawCoverImage(String imageUrl); // Function commented out due to JPEG library issues

void setup() {
  Serial.begin(115200);

   
  VGAController.begin();
  VGAController.setResolution(VGA_640x200_70Hz);  // Using 320x200 resolution with 8 colors to conserve memory for WiFi
  canvas.setPenColor(COLOR_RED);
  canvas.setBrushColor(COLOR_BLACK);
  canvas.clear();

  PS2Controller.begin();
  keyboard = PS2Controller.keyboard();
  keyboard->begin(true,true,0);
  keyboard->setLayout(&fabgl::USLayout);

  showDialog("Connecting...");

  Serial.printf("Attempting to connect to %s\n\r", ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while ( WiFi.status() != WL_CONNECTED ) {
      delay (500);
      Serial.print ( "." );
  }
  showDialog("Connected!");
  // Connect to WiFi
  Serial.print ( "Connected to " );
  Serial.println ( ssid );
  Serial.print ( "IP address: " );
  Serial.println ( WiFi.localIP() );
  Serial.println("");

  // Fetch initial book data
  fetchBooks(currentPage);
}

void loop() {
  handleInput();
}
// Word wrap function that accepts a string and returns a string with newline characters
String wrapText(String text, int maxCharsPerLine = 70) {
  String result = "";
  int len = text.length();
  int start = 0;

  while (start < len) {
    int end = start + maxCharsPerLine;

    // Don't exceed text length
    if (end > len) {
      result += text.substring(start);
      break;
    }

    // If we're at a space or end of string, break here
    if (text.charAt(end) == ' ' || text.charAt(end) == '\n') {
      result += text.substring(start, end) + "\n";
      start = end + 1; // Skip the space
      continue;
    }

    // Look backwards for the last space before maxCharsPerLine
    int lastSpace = -1;
    for (int i = end - 1; i >= start; i--) {
      if (text.charAt(i) == ' ') {
        lastSpace = i;
        break;
      }
    }

    // If we found a space, break there
    if (lastSpace != -1) {
      result += text.substring(start, lastSpace) + "\n";
      start = lastSpace + 1; // Skip the space
    } else {
      // No space found, force break at maxCharsPerLine
      result += text.substring(start, end) + "\n";
      start = end;
    }
  }

  return result;
}

void printBookDetails(Book book) {
  // Hardcoded printer IP and port
  Serial.println("Printing...");
  showDialog("Printing...");
  WiFiClient client;

  if (client.connect(printerIP, printerPort)) {
    Serial.print("Connected to: ");
    Serial.print(printerIP);
    Serial.print(":");
    Serial.print(printerPort);
    Serial.print("/n/r");
    // Send title
    client.print("Title: ");
    client.print(book.title.c_str());
    client.print("\r\n");

    // Send author
    client.print("Author: ");
    client.print(book.author.c_str());
    client.print("\r\n");

    // Send ISBN
    client.print("ISBN: ");
    client.print(book.isbn.c_str());
    client.print("\r\n");

    // Send Location
    client.print("Location: ");
    client.print(book.location.c_str());
    client.print("\r\n");

    // Send Keywords with wrapping
    client.print("Keywords:\r\n");
    String wrappedKeywords = wrapText(book.keywords, 80); // Wrap keywords to 80 chars per line
    int charIndex = 0;
    while (charIndex < wrappedKeywords.length()) {
      int lineEnd = charIndex;
      while (lineEnd < wrappedKeywords.length() && wrappedKeywords.charAt(lineEnd) != '\n') {
        lineEnd++;
      }

      if (lineEnd > charIndex) { // Make sure we have content to send
        String line = wrappedKeywords.substring(charIndex, lineEnd);
        client.print(line.c_str());
        client.print("\r\n");
      }

      if (lineEnd < wrappedKeywords.length() && wrappedKeywords.charAt(lineEnd) == '\n') {
        lineEnd++; // Skip the newline character
      }
      charIndex = lineEnd;
    }

    // Send Summary/Synopsis with word wrapping
    client.print("Summary:\r\n");

    // Wrap the synopsis text and send line by line
    String wrappedSynopsis = wrapText(book.synopsis, 80); // Wrap synopsis to 80 chars per line
    int synopsisCharIndex = 0;
    while (synopsisCharIndex < wrappedSynopsis.length()) {
      int lineEnd = synopsisCharIndex;
      while (lineEnd < wrappedSynopsis.length() && wrappedSynopsis.charAt(lineEnd) != '\n') {
        lineEnd++;
      }

      if (lineEnd > synopsisCharIndex) { // Make sure we have content to send
        String line = wrappedSynopsis.substring(synopsisCharIndex, lineEnd);
        client.print(line.c_str());
        client.print("\r\n");
      }

      if (lineEnd < wrappedSynopsis.length() && wrappedSynopsis.charAt(lineEnd) == '\n') {
        lineEnd++; // Skip the newline character
      }
      synopsisCharIndex = lineEnd;
    }

    client.print("\r\n"); // Extra line at the end
    client.stop();
    Serial.println("Printing complete.");
    showBookDetails(selectedBookIndex);
  } else {
    showDialog("Error!");
    // Connection failed - could log this if needed
  }
}

void showHelpDialog() {
  // Draw dialog background
  canvas.setPenColor(COLOR_WHITE);
  canvas.setBrushColor(COLOR_BLUE);
  canvas.fillRectangle(50, 30, 590, 170);  // Centered dialog

  // Title
  canvas.setPenColor(COLOR_YELLOW);
  canvas.drawText(250, 40, "Keyboard Help");

  // Commands list
  canvas.setPenColor(COLOR_WHITE);
  int startY = 60;
  int lineHeight = 14;

  canvas.drawText(70, startY, "p/P - Previous Page");
  canvas.drawText(320, startY, "n/N - Next Page");

  startY += lineHeight;
  canvas.drawText(70, startY, "i/I - Enter Search Mode");
  canvas.drawText(320, startY, "f/F - Select Search Field");

  startY += lineHeight;
  canvas.drawText(70, startY, "s/S - Search");
  canvas.drawText(320, startY, "r/R - Reset Search");

  startY += lineHeight;
  canvas.drawText(70, startY, "1-8 - View Book Details");
  canvas.drawText(320, startY, "PgUp - Scroll Up (Details)");

  startY += lineHeight;
  canvas.drawText(70, startY, "PgDn - Scroll Down (Details)");
  canvas.drawText(320, startY, "ESC - Exit/Return");

  startY += lineHeight;
  canvas.drawText(70, startY, "h/H - Show Help");
  canvas.drawText(320, startY, "p/P - Print Details");

  // Instructions
  canvas.setPenColor(COLOR_YELLOW);
  canvas.drawText(220, 150, "Press ESC to return");
}

void showBookDetails(int bookIndex) {
  if (bookIndex >= 0 && bookIndex < MAX_BOOKS_PER_PAGE && books[bookIndex].id != 0) {
    Book book = books[bookIndex];

    // Draw dialog background
    canvas.setPenColor(COLOR_WHITE);
    canvas.setBrushColor(COLOR_BLUE);
    canvas.fillRectangle(10, 10, 630, 190);  // Nearly full screen

    // Process synopsis with wrapping first to determine total lines
    String wrappedSynopsis = wrapText(book.synopsis, 70);

    // Count total lines in synopsis
    int totalSynopsisLines = 0;
    int tempIndex = 0;
    while (tempIndex < wrappedSynopsis.length()) {
      int lineEnd = tempIndex;
      while (lineEnd < wrappedSynopsis.length() && wrappedSynopsis.charAt(lineEnd) != '\n') {
        lineEnd++;
      }
      if (lineEnd > tempIndex) totalSynopsisLines++;
      tempIndex = lineEnd + 1;
    }

    // Calculate pagination - determine how many lines we can show per page
    int maxSynopsisLinesPerPagePage0 = 5; // Number of synopsis lines we can fit on page 0 (with static content)
    int maxSynopsisLinesPerPageOther = 13; // Number of synopsis lines we can fit on other pages (no static content)

    // Calculate total pages based on different line counts per page
    totalDetailsPages = 1;
    if (totalSynopsisLines > 0) {
      int remainingLinesAfterPage0 = totalSynopsisLines - maxSynopsisLinesPerPagePage0;
      if (remainingLinesAfterPage0 <= 0) {
        totalDetailsPages = 1; // Only page 0 is needed
      } else {
        totalDetailsPages = 1 + (remainingLinesAfterPage0 + maxSynopsisLinesPerPageOther - 1) / maxSynopsisLinesPerPageOther;
      }
    }

    // Ensure pagination is within bounds
    if (detailsPagination >= totalDetailsPages && totalDetailsPages > 0) {
      detailsPagination = totalDetailsPages - 1;
    } else if (detailsPagination < 0) {
      detailsPagination = 0;
    }

    // Calculate starting line for synopsis based on pagination
    int startLine = 0;
    int displayY = 20; // Start from top of dialog

    // Show static content (title, author, isbn, keywords) only on first page
    if (detailsPagination == 0) {
      // Show static content (title, author, isbn, keywords)
      // Title
      canvas.setPenColor(COLOR_YELLOW);
      canvas.drawText(20, 20, ("Title: " + truncateText(book.title, 70)).c_str());

      // Author
      canvas.setPenColor(COLOR_WHITE);
      canvas.drawText(20, 40, ("Author: " + truncateText(book.author, 70)).c_str());

      // ISBN and Location on the same line
      canvas.setPenColor(COLOR_WHITE);
      canvas.drawText(20, 60, ("ISBN: " + truncateText(book.isbn, 30)).c_str());
      canvas.drawText(320, 60, ("Location: " + truncateText(book.location, 30)).c_str());

      // Process keywords with wrapping
      canvas.setPenColor(COLOR_WHITE);
      String wrappedKeywords = wrapText(book.keywords, 70);
      int keywordY = 80;
      int charIndex = 0;
      int lineCount = 0;
      const int maxKeywordLines = 3;

      // Display wrapped keywords line by line
      while (charIndex < wrappedKeywords.length() && lineCount < maxKeywordLines) {
        int lineEnd = charIndex;
        while (lineEnd < wrappedKeywords.length() && wrappedKeywords.charAt(lineEnd) != '\n') {
          lineEnd++;
        }

        if (lineEnd > charIndex) { // Make sure we have content to display
          String line = wrappedKeywords.substring(charIndex, lineEnd);
          canvas.drawText(20, keywordY, line.c_str());
          keywordY += 12;
          lineCount++;
        }

        charIndex = lineEnd + 1; // Skip the newline character
      }
    }

    // For pagination, show the synopsis content starting from the appropriate line
    if (totalSynopsisLines > 0) {
      // Calculate starting line for synopsis based on pagination
      if (detailsPagination > 0) {
        // For pages > 0, we start from where the previous pages would have left off
        // Page 1 starts after the lines that would have been shown on page 0
        // On page 0, we show static content and then some synopsis lines
        // So page 1 should start from where page 0 left off in the synopsis
        // For simplicity, we'll calculate the starting line based on cumulative lines shown
        // Page 1 starts after 5 lines (from page 0), page 2 starts after 5+14 lines, etc.
        if (detailsPagination == 1) {
          startLine = maxSynopsisLinesPerPagePage0; // Start after the lines shown on page 0
        } else {
          // For pages > 1, calculate cumulative lines shown
          startLine = maxSynopsisLinesPerPagePage0 + (detailsPagination - 1) * maxSynopsisLinesPerPageOther;
        }
        if (startLine >= totalSynopsisLines) {
          startLine = 0; // Reset if out of bounds
          detailsPagination = 0;
        }
      } else {
        startLine = 0; // For page 0, start from the beginning of synopsis
      }

      // Parse the wrapped synopsis to get the lines for this page
      int currentLine = 0;
      int lineStart = 0;

      // Skip to the starting line
      while (currentLine < startLine && lineStart < wrappedSynopsis.length()) {
        int lineEnd = lineStart;
        while (lineEnd < wrappedSynopsis.length() && wrappedSynopsis.charAt(lineEnd) != '\n') {
          lineEnd++;
        }
        lineStart = lineEnd + 1; // Skip the newline character
        currentLine++;
      }

      // Determine where to start displaying based on pagination
      displayY = (detailsPagination == 0) ? 120 : 20; // Start after static content on page 0, from top on other pages

      // Calculate how many lines we can show based on available space
      int maxLinesToShow = (detailsPagination == 0) ? maxSynopsisLinesPerPagePage0 : maxSynopsisLinesPerPageOther;

      // Display up to maxLinesToShow lines for this page
      int linesDisplayed = 0;
      while (lineStart < wrappedSynopsis.length() && linesDisplayed < maxLinesToShow && displayY < 170) { // Leave space for instructions at bottom
        int lineEnd = lineStart;
        while (lineEnd < wrappedSynopsis.length() && wrappedSynopsis.charAt(lineEnd) != '\n') {
          lineEnd++;
        }

        if (lineEnd > lineStart) { // Make sure we have content to display
          String line = wrappedSynopsis.substring(lineStart, lineEnd);
          canvas.setPenColor(COLOR_WHITE);
          canvas.drawText(20, displayY, line.c_str());
          displayY += 12;
          linesDisplayed++;
        }

        lineStart = lineEnd + 1; // Skip the newline character
        currentLine++;
      }
    }

    // Instructions
    canvas.setPenColor(COLOR_YELLOW);
    String instructions = "ESC:Return";
    if (totalSynopsisLines > 0) {
      instructions += " PgUp/Dn:Scroll ";
      instructions += String(detailsPagination + 1) + "/" + String(totalDetailsPages);
    }
    canvas.drawText(20, 180, instructions.c_str());
  }
}

void handleInput() {
  int vkeys = keyboard->virtualKeyAvailable();
  if (vkeys > 0) {
    VirtualKey vk_vk = keyboard->getNextVirtualKey();
    if (!keyboard->isVKDown(vk_vk)) {
      int vk = keyboard->virtualKeyToASCII(vk_vk);
      Serial.println(vk);
      //int scanCode = vk_vk.scancode;

      if (inputMode) {
        // Input mode: handle text input
        if (vk >= 32 && vk <= 126) {  // Printable ASCII characters
          searchQuery += (char)vk;
        } else if (vk == 8) {  // Backspace
          if (searchQuery.length() > 0) {
            searchQuery.remove(searchQuery.length() - 1);
          }
        } else if (vk == 13 || vk == 10) {  // Enter key
          inputMode = false;  // Exit input mode
        }
        drawSearchControls();
      } else if (showSearchDropdown) {
        // Field selection mode
        if (vk == 13 || vk == 10) {  // Enter key - select field
          searchField = searchFields[selectedFieldIndex];
          showSearchDropdown = false;
          drawUI();
        } else if (vk_vk == fabgl::VK_UP) {  // Up arrow
	  Serial.println("UP");
          selectedFieldIndex = (selectedFieldIndex > 0) ? selectedFieldIndex - 1 : 4;
          drawSearchControls();
        } else if (vk_vk == fabgl::VK_DOWN) {  // Down arrow
	  Serial.println("DOWN");
          selectedFieldIndex = (selectedFieldIndex < 4) ? selectedFieldIndex + 1 : 0;
          drawSearchControls();
        } else if (vk == 27) {  // Escape key - cancel field selection
          showSearchDropdown = false;
          drawUI();
        }
      } else if (showDetailsDialog) {
        // Handle details dialog specific commands
        if (vk == 27) {  // ESC key - Return to main view
          showDetailsDialog = false;  // Hide the details dialog
          detailsPagination = 0;  // Reset pagination
          drawUI();  // Redraw the main UI
        } else if (vk == 112 || vk == 80) {  // 'p' or 'P' - Print book details
          printBookDetails(books[selectedBookIndex]);
        } else if (vk_vk == fabgl::VK_PAGEUP) {  // Page Up key
          if (detailsPagination > 0) {
            detailsPagination--;
            showBookDetails(selectedBookIndex);
          }
        } else if (vk_vk == fabgl::VK_PAGEDOWN) {  // Page Down key
          // Use the total pages calculated by the display function
          if (detailsPagination < totalDetailsPages - 1) {
            detailsPagination++;
            showBookDetails(selectedBookIndex);
          }
        }
      } else if (helpDialog) {
        // Handle help dialog specific commands
        if (vk == 27) {  // ESC key - Return to main view
          helpDialog = false;  // Hide the help dialog
          drawUI();  // Redraw the main UI
        }
      } else {
        // Normal mode: handle navigation and other commands
        if (vk == 112 || vk == 80) {  // 'p' or 'P' - Previous page
          if (currentPage > 1) {
            currentPage--;
            fetchBooks(currentPage, searchQuery, searchField);
          }
        } else if (vk == 110 || vk == 78) {  // 'n' or 'N' - Next page
          if (currentPage < totalPages) {
            currentPage++;
            fetchBooks(currentPage, searchQuery, searchField);
          }
        } else if (vk == 105 || vk == 73) {  // 'i' or 'I' - Enter input mode
          inputMode = true;
          drawSearchControls();
        } else if (vk == 102 || vk == 70) {  // 'f' or 'F' - Toggle field dropdown
          showSearchDropdown = !showSearchDropdown;
          if (showSearchDropdown) {
            drawSearchControls();
            selectedFieldIndex = 0;  // Reset to first field
          }
        } else if (vk == 115 || vk == 83) {  // 's' or 'S' - Trigger search
	  currentPage=1;
          fetchBooks(currentPage, searchQuery, searchField);
        } else if (vk == 114 || vk == 82) {  // 'r' or 'R' - Reset search
          currentPage = 1;
          searchQuery = "";
          searchField = searchFields[0];  // Reset to first field
          selectedFieldIndex = 0;
          fetchBooks(currentPage, searchQuery, searchField);
        } else if (vk == 104 || vk == 72) {  // 'h' or 'H' - Show help
          helpDialog = true;
          showDetailsDialog = false;  // Hide details dialog if shown
          detailsPagination = 0;  // Reset pagination
          showHelpDialog();
        } else if (vk >= 49 && vk <= 56) {  // '1'-'8' keys - Show details for books 0-7
          int bookNumber = vk - 49;  // Convert ASCII to 0-7 range
          selectedBookIndex = bookNumber;  // Store the selected book index
          showDetailsDialog = true;  // Show the details dialog
          helpDialog = false;  // Hide help dialog if shown
          showBookDetails(bookNumber);
        } else if (vk_vk == fabgl::VK_UP) {  // Up arrow
	  if (selectedBookIndex > 0 ) { 
            selectedBookIndex--;
          }
          drawTable();
        } else if (vk_vk == fabgl::VK_DOWN) {  // Down arrow
	  if (selectedBookIndex < 7) { 
            selectedBookIndex++;
          }
          drawTable();
        } else if (vk == 13 || vk == 10) {  // Enter key - select field
          showDetailsDialog = true;  // Show the details dialog
          helpDialog = false;  // Hide help dialog if shown
          showBookDetails(selectedBookIndex);
        }
      }
    }
  }
}

void drawUI() {
  canvas.setPenColor(COLOR_RED);
  canvas.setBrushColor(COLOR_BLACK);
  canvas.clear();

  // Draw header
  canvas.setPenColor(COLOR_GREEN);
  canvas.drawText(4, 4, "Adrenlinerush Card Catalog");  // Adjusted for 320x200 resolution
  
  // Draw search controls
  drawSearchControls();
  
  // Draw book table
  drawTable();
  
  // Draw pagination controls
  drawPaginationControls();
  
}

void fetchBooks(int page, String search, String field) {
  showLoadingDialog();
  HTTPClient http;
  String url = String(apiUrl) + "/books?page=" + page + "&limit=" + MAX_BOOKS_PER_PAGE;

  if (search != "") {
    url += "&search=" + search + "&field=" + field;
  }

  http.begin(url);
  http.setTimeout(10000); // 10 second timeout
  http.addHeader("Content-Type", "application/json");

  Serial.println("Requesting URL: " + url);

  int httpResponseCode = http.GET();

  if (httpResponseCode > 0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    String payload = http.getString();
    Serial.println("API Response length: " + String(payload.length()));
    // Only print first 500 chars to avoid flooding serial monitor
    if(payload.length() > 500) {
      Serial.println(payload.substring(0, 500) + "...");
    } else {
      Serial.println(payload);
    }
    parseJson(payload);
  } else {
    showDialog("Error...");
    Serial.print("Error on HTTP request: ");
    Serial.println(httpResponseCode);
    Serial.println("URL attempted: " + url);
  }

  http.end();
  drawUI();
}

void parseJson(String jsonString) {
  DynamicJsonDocument doc(65000); // Increased size to accommodate larger responses
  DeserializationError error = deserializeJson(doc, jsonString);

  if (error) {
    Serial.print("JSON parsing failed: ");
    Serial.println(error.c_str());
    Serial.println("Failed JSON string: " + jsonString);
    return;
  }

  // Check if status is OK
  String status = doc["status"];
  if (status != "ok") {
    Serial.println("API returned error status: " + status);
    return;
  }

  // Extract metadata
  currentPage = doc["page"];
  totalBooks = doc["total_books"];
  totalPages = ceil((float)totalBooks / MAX_BOOKS_PER_PAGE);

  // Extract books
  JsonArray booksArray = doc["books"];
  int count = 0;
  for (JsonObject book : booksArray) {
    books[count].id = book["id"];
    books[count].title = book["title"].as<String>();
    books[count].author = book["author"].as<String>();
    books[count].isbn = book["isbn"].as<String>();
    books[count].location = book["location"].as<String>();
    books[count].keywords = book["keywords"].as<String>();
    books[count].synopsis = book["synopsis"].as<String>();
    books[count].coverUrl = book["cover_url"].as<String>();
    count++;

    if (count >= MAX_BOOKS_PER_PAGE) break; // Limit to MAX_BOOKS_PER_PAGE books
  }

  // Clear remaining slots if we got fewer than MAX_BOOKS_PER_PAGE books
  for (int i = count; i < MAX_BOOKS_PER_PAGE; i++) {
    books[i].id = 0;
    books[i].title = "";
    books[i].author = "";
    books[i].isbn = "";
    books[i].location = "";
    books[i].keywords = "";
    books[i].synopsis = "";
    books[i].coverUrl = "";
  }

  Serial.printf("Parsed %d books\n", count);
}

void drawTable() {
  // Table headers
  canvas.setPenColor(COLOR_WHITE);
  canvas.setBrushColor(COLOR_BLUE);
  canvas.fillRectangle(4, 48, 636, 64);  // Extended width for better visibility (scaled for 640x200)

  canvas.setPenColor(COLOR_YELLOW);
  canvas.drawText(6, 52, "Title");
  canvas.drawText(286, 52, "Author");
  canvas.drawText(583, 52, "Loc");  // Shortened location header

  // Draw table grid
  canvas.setPenColor(COLOR_WHITE);
  canvas.drawLine(4, 64, 636, 64);  // Horizontal line below header

  // Draw table rows
  canvas.setPenColor(COLOR_WHITE);
  for (int i = 0; i < MAX_BOOKS_PER_PAGE; i++) { // Show max MAX_BOOKS_PER_PAGE books at a time
    int bookIndex = i + scrollOffset;
    if (bookIndex >= MAX_BOOKS_PER_PAGE || books[bookIndex].id == 0) break;  // Stop if no more books

    int y = 65.6 + (i * 14.4);  // Scaled from 82 + (i * 18)

    // Alternate row colors
    if (i % 2 == 0) {
      canvas.setBrushColor(COLOR_WHITE);
      canvas.setPenColor(COLOR_BLACK);
    } else {
      canvas.setBrushColor(COLOR_BLACK);
      canvas.setPenColor(COLOR_WHITE);
    }

    canvas.fillRectangle(4, y, 636, y + 12.8);  // Full width for 640x200

    // Highlight selected row
    if (selectedBookIndex == bookIndex) {
      canvas.setBrushColor(COLOR_CYAN);
      canvas.setPenColor(COLOR_BLACK);
      canvas.fillRectangle(4, y, 636, y + 12.8);  // Full width for 640x200
    }

    // Draw cell separators
    canvas.setPenColor(COLOR_WHITE);
    canvas.drawLine(4, y + 12.8, 636, y + 12.8);  // Row separator
    canvas.drawLine(283, y, 283, y + 12.8);      // Title/Author separator
    canvas.drawLine(580, y, 580, y + 12.8);      // Author/Location separator (leaving ~4 pixels for location)

    // Reset pen color for text
    if (selectedBookIndex == bookIndex) {
      canvas.setPenColor(COLOR_BLACK);
    } else if (i % 2 == 0) {
      canvas.setPenColor(COLOR_BLACK);
    } else {
      canvas.setPenColor(COLOR_WHITE);
    }

    canvas.drawText(6, y + 1.6, truncateText(books[bookIndex].title, 35).c_str());  // Title column
    canvas.drawText(286, y + 1.6, truncateText(books[bookIndex].author, 35).c_str());  // Author column
    canvas.drawText(583, y + 1.6, truncateText(books[bookIndex].location, 3).c_str());  // Location column (only 3 chars, ~24 pixels wide)
  }

  // Reset colors
  canvas.setPenColor(COLOR_WHITE);
  canvas.setBrushColor(COLOR_BLACK);
}

String truncateText(String text, int maxLength) {
  if (text.length() <= maxLength) {
    return text;
  }
  return text.substring(0, maxLength - 3) + "...";
}

void drawPaginationControls() {
  canvas.setPenColor(COLOR_WHITE);
  canvas.setBrushColor(COLOR_BLUE);

  // Previous button
  canvas.fillRectangle(4, 188, 44, 200);  // Scaled from 10, 350, 110, 380 (for 320x200)
  canvas.setPenColor(COLOR_YELLOW);
  if (currentPage > 1) {
    canvas.fillRectangle(4.8, 188.8, 43.2, 199.2);  // Scaled from 12, 352, 108, 378
    canvas.setPenColor(COLOR_WHITE);
    canvas.drawText(6, 192, "Prev");  // Scaled from 30, 360
  } else {
    // Gray out button if on first page
    canvas.setPenColor(COLOR_WHITE);
    canvas.drawText(6, 192, "Prev");  // Scaled from 30, 360
  }

  // Next button
  canvas.setPenColor(COLOR_WHITE);
  canvas.setBrushColor(COLOR_BLUE);
  canvas.fillRectangle(48, 188, 88, 200);  // Scaled from 120, 350, 220, 380
  canvas.setPenColor(COLOR_YELLOW);
  if (currentPage < totalPages) {
    canvas.fillRectangle(48.8, 188.8, 87.2, 199.2);  // Scaled from 122, 352, 218, 378
    canvas.setPenColor(COLOR_WHITE);
    canvas.drawText(50, 192, "Next");  // Scaled from 150, 360
  } else {
    // Gray out button if on last page
    canvas.setPenColor(COLOR_WHITE);
    canvas.drawText(50, 192, "Next");  // Scaled from 150, 360
  }

  // Page info
  canvas.setPenColor(COLOR_WHITE);
  canvas.setBrushColor(COLOR_BLACK);
  String pageInfo = "Page " + String(currentPage) + " of " + String(totalPages) + " (Total Books: " + String(totalBooks) + ")";
  canvas.drawText(96, 192, pageInfo.c_str());  // Scaled from 240, 360
}

void drawSearchControls() {
  canvas.setPenColor(COLOR_WHITE);
  canvas.setBrushColor(COLOR_BLUE);

  // Search label
  canvas.drawText(4, 28, "Search:");  // Scaled from 10, 35

  // Search input box - moved over and made wider
  canvas.fillRectangle(60, 24, 305, 40);  // Moved right and made wider
  canvas.setPenColor(COLOR_BLACK);
  canvas.setBrushColor(COLOR_WHITE);
  canvas.fillRectangle(61, 25.6, 303, 38.4);  // Adjusted to match
  canvas.setPenColor(COLOR_BLACK);
  String searchText = searchQuery;
  if (inputMode) { searchText = searchText + "_"; }
  canvas.drawText(63, 28, searchText.c_str());  // Moved to match input box

  // Field dropdown - moved over and made wider
  canvas.setPenColor(COLOR_WHITE);
  canvas.setBrushColor(COLOR_BLUE);
  canvas.fillRectangle(315, 24, 400, 40);  // Moved right and made wider
  canvas.setPenColor(COLOR_YELLOW);
  canvas.drawText(317, 28, searchField.c_str());  // Moved to match dropdown

  // Dropdown arrow
  canvas.setPenColor(COLOR_WHITE);
  canvas.drawLine(395, 28, 399, 28);  // Top line of arrow (at right edge)
  canvas.drawLine(395, 28, 397, 30);  // Left diagonal
  canvas.drawLine(399, 28, 397, 30);  // Right diagonal

  // Show dropdown options if expanded
  if (showSearchDropdown) {
    canvas.setPenColor(COLOR_WHITE);
    canvas.setBrushColor(COLOR_BLUE);
    for (int i = 0; i < 5; i++) {
      canvas.fillRectangle(4, 40 + (i * 16), 316, 56 + (i * 16));  // Full screen width from 4 to 316
      canvas.setPenColor(COLOR_WHITE);
      if (i == selectedFieldIndex) {
        canvas.setBrushColor(COLOR_CYAN);
        canvas.fillRectangle(4, 40 + (i * 16), 316, 56 + (i * 16));  // Full screen width from 4 to 316
        canvas.setBrushColor(COLOR_BLUE);
      }
      canvas.drawText(6, 44 + (i * 16), searchFields[i].c_str());  // Starting from x=6
    }
  }

  // Search button - moved over and made wider
  canvas.setPenColor(COLOR_WHITE);
  canvas.setBrushColor(COLOR_BLUE);
  canvas.fillRectangle(410, 24, 500, 40);  // Moved right and made wider
  canvas.setPenColor(COLOR_YELLOW);
  canvas.drawText(430, 28, "Search");  // Centered in button
}

void drawDetailsDialog() {
  // Dialog background
  canvas.setPenColor(COLOR_WHITE);
  canvas.setBrushColor(COLOR_BLUE);
  canvas.fillRectangle(20, 40, 300, 152);  // Extended size for better layout (scaled from 50, 50, 750, 380 for 320x200)

  if (selectedBookIndex >= 0 && selectedBookIndex < MAX_BOOKS_PER_PAGE) {
    Book book = books[selectedBookIndex];

    // Title
    canvas.setPenColor(COLOR_YELLOW);
    canvas.drawText(24, 48, ("Title: " + book.title).c_str());  // Scaled from 60, 60

    // Author
    canvas.setPenColor(COLOR_WHITE);
    canvas.drawText(24, 64, ("Author: " + book.author).c_str());  // Scaled from 60, 80

    // ISBN
    canvas.drawText(24, 80, ("ISBN: " + book.isbn).c_str());  // Scaled from 60, 100

    // Location
    canvas.drawText(24, 96, ("Location: " + book.location).c_str());  // Scaled from 60, 120

    // Keywords (truncated)
    canvas.drawText(24, 112, ("Keywords: " + truncateText(book.keywords, 80)).c_str());  // Scaled from 60, 140

    // Cover image area
    canvas.setPenColor(COLOR_WHITE);
    canvas.drawRectangle(220, 48, 292, 72);  // Scaled from 550, 60, 730, 180 (using drawRectangle instead of drawRect)
    canvas.drawText(228, 96, "Cover Image");  // Scaled from 570, 120

    // If we have a cover URL, attempt to display it
    if (book.coverUrl != "") {
      canvas.drawText(224, 112, "Loading...");  // Scaled from 560, 140
      // Note: Actual image loading would require additional implementation
      // This is a placeholder for where the image would appear
    }

    // Synopsis header
    canvas.setPenColor(COLOR_YELLOW);
    canvas.drawText(24, 136, "Synopsis:");  // Scaled from 60, 170

    // Synopsis text with scrolling capability
    canvas.setPenColor(COLOR_WHITE);
    String synopsis = book.synopsis;
    int lineHeight = 9.6;  // Scaled from 12
    int startY = 148;  // Scaled from 185
    int maxWidth = 192;  // Width available for text (scaled from 480 for 320x200)

    // Simple text wrapping for synopsis
    int charIndex = 0;
    int currentY = startY;
    int linesDisplayed = 0;
    const int maxLines = 6;  // Max lines that fit in the dialog (reduced for 320x200)

    while (charIndex < synopsis.length() && currentY < 140 && linesDisplayed < maxLines) {  // Scaled from currentY < 350
      // Find end of line that fits in width
      int lineEnd = charIndex;
      int lineWidth = 0;

      // Find where to break the line
      while (lineEnd < synopsis.length() && lineWidth < maxWidth) {
        char c = synopsis.charAt(lineEnd);
        if (c == '\n') {
          break;  // Line break in text
        }
        lineWidth += 4.8;  // Approximate character width (scaled from 6)
        lineEnd++;
      }

      // If we went too far, try to break at word boundary
      if (lineWidth > maxWidth) {
        int wordBreak = lineEnd;
        while (wordBreak > charIndex && synopsis.charAt(wordBreak) != ' ') {
          wordBreak--;
        }
        if (wordBreak > charIndex) {
          lineEnd = wordBreak;
        }
      }

      String line = synopsis.substring(charIndex, lineEnd);
      canvas.drawText(24, currentY, line.c_str());  // Scaled from 60, currentY
      currentY += lineHeight;
      charIndex = lineEnd;
      linesDisplayed++;

      // Skip spaces after line break
      while (charIndex < synopsis.length() &&
             (synopsis.charAt(charIndex) == ' ' || synopsis.charAt(charIndex) == '\n')) {
        charIndex++;
      }
    }

    // Close button
    canvas.setPenColor(COLOR_WHITE);
    canvas.setBrushColor(COLOR_RED);
    canvas.fillRectangle(260, 140, 288, 148);  // Scaled from 650, 350, 720, 370
    canvas.setPenColor(COLOR_YELLOW);
    canvas.drawText(266, 142, "Close");  // Scaled from 665, 355
  }
}
void showLoadingDialog() {
   showDialog("Loading...");
}

void showDialog(String message) {
   // Draw dialog box
   canvas.setPenColor(COLOR_WHITE);
   canvas.setBrushColor(COLOR_BLUE);
   int dialogWidth = 200;
   int dialogHeight = 60;
   int startX = (canvas.getWidth() - dialogWidth) / 2;
   int startY = (canvas.getHeight() - dialogHeight) / 2;
  
   canvas.fillRectangle(startX, startY, startX + dialogWidth, startY + dialogHeight);
   canvas.setPenColor(COLOR_WHITE);
   canvas.drawText(startX + (dialogWidth/2) - 30, startY + (dialogHeight/2) - 8, message.c_str());
}

