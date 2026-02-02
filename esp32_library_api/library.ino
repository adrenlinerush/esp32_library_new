#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <sqlite3.h>
#include <SPI.h>
#include <FS.h>
#include "SD.h"
#include <ESPmDNS.h>
#include <map>
#include <time.h>
#include <ArduinoJson.h>

std::map<String, String> config;
std::map<String, String> sessions;

IPAddress allowedHost;

const int DEFAULT_RESULTS_PER_PAGE = 50;
char* errMsg;
sqlite3 *db;
int rc;
sqlite3_stmt *res;
int rec_count = 0;
const char *tail;
char current_db[255];
const char* db_filename = "/sd/library.db";

WebServer server(80);

const char* headersToCollect[] = {"Cookie"};

void sendJson(int code, JsonDocument &doc) {
  String out;
  serializeJson(doc, out);
  server.send(code, "application/json", out);
}

IPAddress parseIPAddress(const String &ipStr) {
    int parts[4] = {0};
    int partIndex = 0;

    int start = 0;
    for (int i = 0; i < ipStr.length(); i++) {
        if (ipStr.charAt(i) == '.' || i == ipStr.length() - 1) {
            if (i == ipStr.length() - 1) i++;
            parts[partIndex++] = ipStr.substring(start, i).toInt();
            start = i + 1;
        }
    }

    return IPAddress(parts[0], parts[1], parts[2], parts[3]);
}

void readConfig(fs::FS &fs){
  Serial.println("Openning config file...");
  File configFile = fs.open("/config", FILE_READ);
  if (configFile) {
    Serial.println("Reading config ...");
    while (configFile.available()) {
      String line = configFile.readStringUntil('\n');
      unsigned eq = line.indexOf("=");
      String key = line.substring(0,eq);
      //Serial.printf("Found key: %s\n", key);
      String value = line.substring(eq+1);
      config[key]=value;
      if (key == "proxy_host") {
        allowedHost = parseIPAddress(value);
      } 
    }
    configFile.close(); 
  } 
  else {
    Serial.println("error opening config");
  }
}


String generateSessionToken() {
    return String(random(100000, 999999));
}

bool is_admin() {
  IPAddress clientIP = server.client().remoteIP();
  String token = server.header("Cookie");
  token = token.substring(token.indexOf("session=") + 8);
  bool has_token = sessions.count(token) > 0;
  if (has_token && clientIP == allowedHost) {
    return true;
  } else {
    return false;
  }
}



void handleAddBook() {
  StaticJsonDocument<512> doc;
  if (is_admin()) {


    String bodyStr = server.arg("plain");
    StaticJsonDocument<1024> body;
    DeserializationError error = deserializeJson(body, bodyStr);
    if (error) {
      doc["status"] = "error";
      doc["error"] = "Failed parse json!";
      sendJson(500, doc);
      return;
    }

    const char* sql = "INSERT INTO books (title, author, isbn, location, keywords, synopsis) VALUES (?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, body["title"].as<const char*>(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, body["author"].as<const char*>(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, body["isbn"].as<const char*>(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, body["location"].as<const char*>(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, body["keywords"].as<const char*>(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, body["synopsis"].as<const char*>(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) == SQLITE_DONE) {
          doc["status"] = "ok";
          sendJson(201, doc);
          return;
        } else {
          doc["status"] = "error";
          doc["error"] = "Failed to add book!";
          sendJson(500, doc);
          return;
        }
    } else {
        doc["status"] = "error";
        doc["error"] = "Failed prepare statement!";
        sendJson(500, doc);
        return;
    }
    sqlite3_finalize(stmt);
  } else {
    doc["status"] = "error";
    doc["error"] = "You do not have permission to perform requested action!";
    sendJson(401, doc);
    return;
  }
}

void handleAuthenticate() {
  // Read JSON body
  String bodyStr = server.arg("plain");
  StaticJsonDocument<512> body;
  DeserializationError error = deserializeJson(body, bodyStr);
  StaticJsonDocument<256> doc;

  if (error) {
    doc["status"] = "error";
    doc["error"] = "Invalid JSON";
    sendJson(400, doc);
    return;
  }

  const char* username = body["username"];
  const char* password = body["password"];

  if (username && password && strcmp(username, "admin") == 0 && strcmp(password, config["admin_password"].c_str()) == 0) {
    // Generate session token
    String token = generateSessionToken();
    Serial.println(token);
    sessions[token] = username;

    doc["status"] = "ok";
    doc["token"] = token;  // send token in JSON
    sendJson(200, doc);
  } else {
    doc["status"] = "error";
    doc["error"] = "Invalid credentials";
    sendJson(401, doc);
  }
}

void handleLogout() {
  StaticJsonDocument<256> doc;
  String token = server.header("Cookie");
  token = token.substring(token.indexOf("session=") + 8);

  if (is_admin()) {
    sessions.erase(token);
  }
  doc["status"] = "ok";
  sendJson(201, doc);
}

void handleNotFound() {
  StaticJsonDocument<512> doc;

  doc["status"] = "error";
  doc["error"] = "File Not Found";
  doc["uri"] = server.uri();
  doc["method"] = (server.method() == HTTP_GET) ? "GET" : "POST";
  doc["arguments"] = server.args();

  JsonArray args = doc.createNestedArray("args");
  for (uint8_t i = 0; i < server.args(); i++) {
    StaticJsonDocument<128> arg;
    arg["name"] = server.argName(i);
    arg["value"] = server.arg(i);
    args.add(arg);
  }

  sendJson(404, doc);
}

void handleViewBooks() {
  int page = server.arg("page").toInt();
  if (page < 1) {
    page = 1;
  }

  int resultsPerPage = DEFAULT_RESULTS_PER_PAGE;
  if (server.hasArg("limit")) {
    int limitArg = server.arg("limit").toInt();
    if (limitArg > 0) {
      resultsPerPage = limitArg;
    }
  }

  String search = server.arg("search");
  String field = server.arg("field");

  int offset = (page - 1) * resultsPerPage;

  StaticJsonDocument<65536> doc;
  doc["status"] = "ok";
  doc["page"] = page;
  doc["results_per_page"] = resultsPerPage;
  bool useSearch = (search != "" && field != "");

  // Count total books
  String sqlCount = "SELECT COUNT(*) FROM books";
  if (useSearch) {
    sqlCount += " WHERE " + field + " LIKE ?;";
  } else {
    sqlCount += ";";
  }
  int totalBooks = 0;
  sqlite3_stmt* stmtCount;
  if (sqlite3_prepare_v2(db, sqlCount.c_str(), -1, &stmtCount, nullptr) == SQLITE_OK) {
    if (useSearch) {
      String likeTerm = "%" + search + "%";
      sqlite3_bind_text(stmtCount, 1, likeTerm.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (sqlite3_step(stmtCount) == SQLITE_ROW) {
      totalBooks = sqlite3_column_int(stmtCount, 0);
    }
  }
  sqlite3_finalize(stmtCount);
  doc["total_books"] = totalBooks;

  // Build SQL query
  String sql = "SELECT * FROM books";
  if (useSearch) {
    sql += " WHERE " + field + " LIKE ?";
  }
  sql += " LIMIT ? OFFSET ?;";

  sqlite3_stmt* stmt;
  JsonArray books = doc.createNestedArray("books");
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
    int bindIndex = 1;

    if (useSearch) {
      String likeTerm = "%" + search + "%";
      sqlite3_bind_text(stmt, bindIndex++, likeTerm.c_str(), -1, SQLITE_TRANSIENT);
    }

    sqlite3_bind_int(stmt, bindIndex++, resultsPerPage);
    sqlite3_bind_int(stmt, bindIndex, offset);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      StaticJsonDocument<256> book;
      book["id"] = sqlite3_column_int(stmt, 0);
      book["title"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
      book["author"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
      book["isbn"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
      book["location"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
      book["keywords"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
      book["synopsis"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
      book["cover_url"] = "https://covers.openlibrary.org/b/isbn/" + 
                          String(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3))) +
                          "-L.jpg";
      books.add(book);
    }
  } else {
    doc["status"] = "error";
    doc["error"] = "Failed to retrieve books!";
  }

  sqlite3_finalize(stmt);

  sendJson(200, doc);
}

void handleEditSubmit() {
  if (!is_admin()) {
    StaticJsonDocument<256> doc;
    doc["status"] = "error";
    doc["error"] = "You do not have permission to perform this action";
    sendJson(401, doc);
    return;
  }

  // Parse JSON body
  String bodyStr = server.arg("plain");
  StaticJsonDocument<1024> body;
  DeserializationError error = deserializeJson(body, bodyStr);
  StaticJsonDocument<256> doc;

  if (error) {
    doc["status"] = "error";
    doc["error"] = "Invalid JSON";
    sendJson(400, doc);
    return;
  }

  int bookID = body["id"] | 0;  // fallback to 0
  const char* title = body["title"];
  const char* author = body["author"];
  const char* isbn = body["isbn"];
  const char* location = body["location"];
  const char* keywords = body["keywords"];
  const char* synopsis = body["synopsis"];

  if (bookID <= 0 || !title || !author) {
    doc["status"] = "error";
    doc["error"] = "Missing required fields";
    sendJson(400, doc);
    return;
  }

  const char* sql = "UPDATE books SET title = ?, author = ?, isbn = ?, location = ?, keywords = ?, synopsis = ? WHERE id = ?";
  sqlite3_stmt* stmt;

  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
    sqlite3_bind_text(stmt, 1, title, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, author, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, isbn, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, location, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, keywords, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, synopsis, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, bookID);

    if (sqlite3_step(stmt) == SQLITE_DONE) {
      doc["status"] = "ok";
      doc["message"] = "Book updated successfully";
      doc["id"] = bookID;
      sendJson(200, doc);
    } else {
      doc["status"] = "error";
      doc["error"] = String("Failed to update book: ") + sqlite3_errmsg(db);
      sendJson(500, doc);
    }
  } else {
    doc["status"] = "error";
    doc["error"] = String("Database error: ") + sqlite3_errmsg(db);
    sendJson(500, doc);
  }

  sqlite3_finalize(stmt);
}

void handleDeleteBook() {
  if (!is_admin()) {
    StaticJsonDocument<256> doc;
    doc["status"] = "error";
    doc["error"] = "You do not have permission to perform this action";
    sendJson(401, doc);
    return;
  }

  // Parse JSON body
  String bodyStr = server.arg("plain");
  StaticJsonDocument<256> body;
  DeserializationError error = deserializeJson(body, bodyStr);
  StaticJsonDocument<256> doc;

  if (error) {
    doc["status"] = "error";
    doc["error"] = "Invalid JSON";
    sendJson(400, doc);
    return;
  }

  int id = body["id"] | 0;  // default to 0 if missing
  if (id <= 0) {
    doc["status"] = "error";
    doc["error"] = "Missing or invalid book ID";
    sendJson(400, doc);
    return;
  }

  const char* sql = "DELETE FROM books WHERE id = ?;";
  sqlite3_stmt* stmt;

  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
    sqlite3_bind_int(stmt, 1, id);

    if (sqlite3_step(stmt) == SQLITE_DONE) {
      doc["status"] = "ok";
      doc["message"] = "Book deleted successfully";
      doc["id"] = id;
      sendJson(200, doc);
    } else {
      doc["status"] = "error";
      doc["error"] = String("Failed to delete book: ") + sqlite3_errmsg(db);
      sendJson(500, doc);
    }
  } else {
    doc["status"] = "error";
    doc["error"] = String("Database error: ") + sqlite3_errmsg(db);
    sendJson(500, doc);
  }

  sqlite3_finalize(stmt);
}

int openDb(const char *filename) {
  //sqlite3_initialize();
  int rc = sqlite3_open(filename, &db);
  if (rc) {
      Serial.printf("Can't open database: %s\n", sqlite3_errmsg(db));
      memset(current_db, '\0', sizeof(current_db));
      return rc;
  } else {
      Serial.printf("Opened database successfully\n");
      strcpy(current_db, filename);
  }
  return rc;
}

void handleBackup() {
  Serial.println("Backup called...");
  if (is_admin()) {
    Serial.println("is authenticated...");
    sqlite3_close(db);
    Serial.println("db stopped...");
    const char* filename = strrchr(db_filename, '/');
    File dbFile = SD.open(filename, FILE_READ);
    if (!dbFile) {
    	Serial.println("UNABLE TO OPEN FILE!");
	server.send(500, "text/plain", "Failed to retreive backup.");
	return;
    }
    Serial.println("file open...");
  
    time_t now = time(nullptr);
    String backupFilename = "library_db_backup-" + String(now) + ".db";
    Serial.println("Sending backup...");

    server.setContentLength(dbFile.size());
    server.sendHeader("Content-Type", "application/octet-stream");
    server.sendHeader("Content-Disposition", "attachment; filename="+backupFilename);
    server.send(200);
  
    uint8_t buffer[128];
    while (dbFile.available()) {
      size_t bytesRead = dbFile.read(buffer, sizeof(buffer));
      server.client().write(buffer, bytesRead);
    }
    Serial.println("Sent...");
  
    dbFile.close();
    Serial.println("closded file...");
    
    openDb(db_filename);
    Serial.println("reopened db...");
  }
}

void displayImageFiles() {
    String filePath = server.uri();
    if (filePath == "/img") {
      String name = server.arg("name");
      filePath = "/img/" + name;
    }

    if (SD.exists(filePath)) {
        File file = SD.open(filePath);

        String mimeType;
        if (filePath.endsWith(".jpg") || filePath.endsWith(".jpeg")) {
            mimeType = "image/jpeg";
        } else if (filePath.endsWith(".png")) {
            mimeType = "image/png";
        } else if (filePath.endsWith(".gif")) {
            mimeType = "image/gif";
        } else {
            mimeType = "application/octet-stream";
        }
        server.sendHeader("Cache-Control", "max-age=3600");
        server.streamFile(file, mimeType);
        file.close();
    } else {
        server.send(404, "text/plain", "404 Not Found");
    }
}

void handleFrontend() {
  String html = R"=====(
<!DOCTYPE html>
<html>
<head>
  <title>Adrenlinerush Library Catalog</title>
  <style>
    body {
      background-color: #cccccc;
      font-family: Arial, Helvetica, Sans-Serif;
      font-size: large;
      Color: #000088;
    }
    a {
      display: inline-block;
      padding: 10px 15px;
      background: #aaa;
      border: 1px solid #777;
      border-bottom: none;
      border-radius: 4px 4px 0 0;
      margin-right: 1px;
      color: #fff;
      text-decoration: none;
      margin: 5px;
    }
    table {
      border-collapse: collapse;
      width: 100%;
    }
    th, td {
      border: 1px solid #ddd;
      padding: 8px;
      text-align: left;
    }
    th {
      background-color: #f2f2f2;
    }
    form {
      margin: 10px 0;
    }
    input, textarea, select {
      margin: 5px 0;
      padding: 5px;
      width: 50%;
    }
    button {
      padding: 10px 15px;
      margin: 5px;
      cursor: pointer;
    }
    .container {
      max-width: 1200px;
      margin: 0 auto;
      padding: 20px;
    }
    .nav-bar {
      margin-bottom: 20px;
    }
    .spinner-container {
      position: fixed;
      top: 0;
      left: 0;
      width: 100%;
      height: 100%;
      background-color: rgba(255, 255, 255, 0.8); /* Optional overlay */
      display: none; /* Hidden by default */
      justify-content: center;
      align-items: center;
      z-index: 9999; /* Ensure it's on top */
    }

    .loader {
      border: 16px solid #f3f3f3; /* Light grey border */
      border-top: 16px solid #3498db; /* Blue border for the spinning part */
      border-radius: 50%;
      width: 120px;
      height: 120px;
      animation: spin 2s linear infinite; /* Apply the spin animation */
    }

    @keyframes spin {
      0% { transform: rotate(0deg); }
      100% { transform: rotate(360deg); }
    }
  </style>
</head>
<body>
  <div class="spinner-container" id="loading-spinner">
    <div class="loader"></div>
  </div>
  <div class="container">
    <h2>Adrenlinerush Library Catalog</h2>
    <p><img src="/img?name=adrenaline.png" alt="Library Logo"></p>

    <div class="nav-bar">
      <a href="#" onclick="showHome()">Home</a>
      <a href="#" onclick="showSearch()">Search</a>
      <a href="#" onclick="showAddBook()" id="addLink" style="display:none;">Add</a>
      <a href="#" onclick="showLogin()" id="loginLink">Log In</a>
      <a href="#" onclick="logout()" id="logoutLink" style="display:none;">Log Out</a>
    </div>

    <div id="content">
      <!-- Content will be loaded here -->
    </div>
  </div>

  <script>
    let isAdmin = false;
    let currentToken = null;

    // Check admin status on load
    window.onload = function() {
      showHome();
    };

    function showSpinner() {
      document.getElementById('loading-spinner').style.display = 'flex';
    }

    // Function to hide the spinner
    function hideSpinner() {
      document.getElementById('loading-spinner').style.display = 'none';
      // Optional: show content after hiding the spinner
      // document.getElementById('content').style.display = 'block';
    }

    function showHome(page = 1) {
      fetchBooks(page);
    }

    function showSearch() {
      document.getElementById('content').innerHTML = `
        <h2>Search Books</h2>
        <form onsubmit="performSearch(event)">
          <label for="search">Search term:</label>
          <input type="text" id="search" required>
          <br/>

          <label for="field">Search by:</label>
          <select id="field">
            <option value="title">Title</option>
            <option value="author">Author</option>
            <option value="isbn">ISBN</option>
            <option value="location">Location</option>
            <option value="keywords">Keywords</option>
            <option value="synopsis">Synopsis</option>
          </select>
          <br/>

          <button type="submit">Search</button>
          <button type="button" onclick="showHome()">Cancel</button>
        </form>
      `;
    }

    function performSearch(event) {
      event.preventDefault();
      const searchTerm = document.getElementById('search').value;
      const searchField = document.getElementById('field').value;

      fetchBooks(1, 15, searchTerm, searchField);
    }

    function showAddBook() {
      if (!isAdmin) {
        alert('You must be logged in as admin to add books');
        return;
      }

      document.getElementById('content').innerHTML = `
        <h2>Add a New Book</h2>
        <form onsubmit="addBook(event)">
          <label for="title">Title:</label>
          <input type="text" id="title" required>
          <br/>

          <label for="author">Author:</label>
          <input type="text" id="author" required>
          <br/>

          <label for="isbn">ISBN:</label>
          <input type="text" id="isbn">
          <br/>

          <label for="location">Location:</label>
          <input type="text" id="location">
          <br/>

          <label for="keywords">Keywords:</label>
          <input type="text" id="keywords">
          <br/>

          <label for="synopsis">Synopsis:</label>
          <br/>
          <textarea id="synopsis"></textarea>
          <br/>

          <button type="submit">Add Book</button>
          <button type="button" onclick="showHome()">Cancel</button>
        </form>
      `;
    }

    function showLogin() {
      document.getElementById('content').innerHTML = `
        <h2>Login</h2>
        <form onsubmit="login(event)">
          <label for="username">Username:</label>
          <input type="text" id="username" required>
          <br/>

          <label for="password">Password:</label>
          <input type="password" id="password" required>
          <br/>

          <button type="submit">Login</button>
        </form>
      `;
    }

    function login(event) {
      event.preventDefault();

      const username = document.getElementById('username').value;
      const password = document.getElementById('password').value;

      showSpinner();
      fetch('/authenticate', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json'
        },
        body: JSON.stringify({
          username: username,
          password: password
        })
      })
      .then(response => response.json())
      .then(data => {
        hideSpinner();
        if (data.status === 'ok') {
          // Set the session cookie so it's automatically sent with future requests
          document.cookie = "session=" + data.token + "; path=/";
          currentToken = data.token;
          isAdmin = true;
          document.getElementById('addLink').style.display = 'inline-block';
          document.getElementById('logoutLink').style.display = 'inline-block';
          document.getElementById('loginLink').style.display = 'none';
          showHome();
        } else {
          alert('Login failed: ' + data.error);
        }
      })
      .catch(error => {
        hideSpinner();
        alert('Login error: ' + error.message);
      });
    }

    function logout() {
      showSpinner();
      fetch('/logout', {
        method: 'GET'
      })
      .then(response => response.json())
      .then(data => {
        hideSpinner();
        // Clear the session cookie
        document.cookie = "session=; expires=Thu, 01 Jan 1970 00:00:00 UTC; path=/;";
        localStorage.removeItem('sessionToken');
        currentToken = null;
        isAdmin = false;
        document.getElementById('addLink').style.display = 'none';
        document.getElementById('logoutLink').style.display = 'none';
        document.getElementById('loginLink').style.display = 'inline-block';
        showHome();
      })
      .catch(error => {
        hideSpinner();
        console.error('Logout error:', error);
        // Clear the session cookie anyway
        document.cookie = "session=; expires=Thu, 01 Jan 1970 00:00:00 UTC; path=/;";
        localStorage.removeItem('sessionToken');
        currentToken = null;
        isAdmin = false;
        document.getElementById('addLink').style.display = 'none';
        document.getElementById('logoutLink').style.display = 'none';
        document.getElementById('loginLink').style.display = 'inline-block';
        showHome();
      });
    }

    function fetchBooks(page = 1, limit = 15, search = '', field = '') {
      showSpinner();
      let url = '/books?page=' + page + '&limit=' + limit;
      if (search && field) {
        url += '&search=' + encodeURIComponent(search) + '&field=' + encodeURIComponent(field);
      }

      fetch(url)
        .then(response => response.json())
        .then(data => {
          if (data.status === 'ok') {
            displayBooks(data);
          } else {
            document.getElementById('content').innerHTML = '<p>Error loading books: ' + data.error + '</p>';
          }
          hideSpinner(); // Hide spinner after processing data
        })
        .catch(error => {
          document.getElementById('content').innerHTML = '<p>Error loading books: ' + error.message + '</p>';
          hideSpinner(); // Hide spinner on error too
        });
    }

    function displayBooks(data) {
      const books = data.books;
      const page = data.page;
      const totalPages = Math.ceil(data.total_books / data.results_per_page);

      let html = '<h2>Books (' + data.total_books + ' total)</h2>';

      html += '<table>';
      html += '<tr><th>Title</th><th>Author</th><th>ISBN</th><th>Location</th><th>Keywords</th><th>Actions</th></tr>';

      books.forEach(book => {
        html += '<tr>';
        html += '<td>' + book.title + '</td>';
        html += '<td>' + book.author + '</td>';
        html += '<td>' + book.isbn + '</td>';
        html += '<td>' + book.location + '</td>';
        html += '<td>' + book.keywords + '</td>';
        html += '<td>';
        html += '<button onclick="showBookDetails(' + JSON.stringify(book).replace(/"/g, '&quot;') + ')">Details</button>';
        if (isAdmin) {
          html += '<button onclick="showEditBook(' + JSON.stringify(book).replace(/"/g, '&quot;') + ')">Edit</button>';
          html += '<button onclick="deleteBook(' + book.id + ')">Delete</button>';
        }
        html += '</td>';
        html += '</tr>';
      });

      html += '</table>';

      // Pagination
      html += '<div>';
      if (page > 1) {
        html += '<button onclick="fetchBooks(' + (page - 1) + ')">Previous</button> ';
      }
      if (page < totalPages) {
        html += '<button onclick="fetchBooks(' + (page + 1) + ')">Next</button>';
      }
      html += '</div>';

      document.getElementById('content').innerHTML = html;
    }

    function showBookDetails(book) {
      document.getElementById('content').innerHTML = `
        <h2>Book Details</h2>
        <p><strong>Title:</strong> ${book.title}</p>
        <p><strong>Author:</strong> ${book.author}</p>
        <p><strong>ISBN:</strong> ${book.isbn}</p>
        <p><strong>Location:</strong> ${book.location}</p>
        <p><strong>Keywords:</strong> ${book.keywords}</p>
        <p><strong>Synopsis:</strong> ${book.synopsis}</p>
        <table><tr><th>Cover</th></tr>
        <tr><td><img src="${book.cover_url}" style="max-width: 300px;"></td>
        </tr></table>
        <button onclick="showHome()">Back to Home</button>
        ${isAdmin ? '<button onclick="showEditBook(' + book.id + ')">Edit</button>' : ''}
      `;
    }

    function showEditBook(book) {
      if (!isAdmin) {
        alert('You must be logged in as admin to edit books');
        return;
      }

      document.getElementById('content').innerHTML = `
        <h2>Edit Book</h2>
        <form onsubmit="updateBook(event, ${book.id})">
          <input type="hidden" id="edit-book-id" value="${book.id}">

          <label for="edit-title">Title:</label>
          <input type="text" id="edit-title" value="${book.title}" required>
          <br/>

          <label for="edit-author">Author:</label>
          <input type="text" id="edit-author" value="${book.author}" required>
          <br/>

          <label for="edit-isbn">ISBN:</label>
          <input type="text" id="edit-isbn" value="${book.isbn}">
          <br/>

          <label for="edit-location">Location:</label>
          <input type="text" id="edit-location" value="${book.location}">
          <br/>

          <label for="edit-keywords">Keywords:</label>
          <input type="text" id="edit-keywords" value="${book.keywords}">
          <br/>

          <label for="edit-synopsis">Synopsis:</label>
          <br/>
          <textarea id="edit-synopsis">${book.synopsis}</textarea>
          <br/>

          <button type="submit">Update Book</button>
          <button type="button" onclick="showHome()">Cancel</button>
        </form>
      `;
    }

    function addBook(event) {
      event.preventDefault();

      const bookData = {
        title: document.getElementById('title').value,
        author: document.getElementById('author').value,
        isbn: document.getElementById('isbn').value,
        location: document.getElementById('location').value,
        keywords: document.getElementById('keywords').value,
        synopsis: document.getElementById('synopsis').value
      };

      showSpinner();
      fetch('/add', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
        },
        body: JSON.stringify(bookData)
      })
      .then(response => response.json())
      .then(data => {
        hideSpinner();
        if (data.status === 'ok') {
          alert('Book added successfully!');
          showHome();
        } else {
          alert('Error adding book: ' + data.error);
        }
      })
      .catch(error => {
        hideSpinner();
        alert('Error adding book: ' + error.message);
      });
    }

    function updateBook(event, id) {
      event.preventDefault();

      const bookData = {
        id: id,
        title: document.getElementById('edit-title').value,
        author: document.getElementById('edit-author').value,
        isbn: document.getElementById('edit-isbn').value,
        location: document.getElementById('edit-location').value,
        keywords: document.getElementById('edit-keywords').value,
        synopsis: document.getElementById('edit-synopsis').value
      };

      showSpinner();
      fetch('/edit', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
        },
        body: JSON.stringify(bookData)
      })
      .then(response => response.json())
      .then(data => {
        hideSpinner();
        if (data.status === 'ok') {
          alert('Book updated successfully!');
          showHome();
        } else {
          alert('Error updating book: ' + data.error);
        }
      })
      .catch(error => {
        hideSpinner();
        alert('Error updating book: ' + error.message);
      });
    }

    function deleteBook(id) {
      if (!confirm('Are you sure you want to delete this book?')) {
        return;
      }

      const bookData = { id: id };

      showSpinner();
      fetch('/delete', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
        },
        body: JSON.stringify(bookData)
      })
      .then(response => response.json())
      .then(data => {
        hideSpinner();
        if (data.status === 'ok') {
          alert('Book deleted successfully!');
          showHome();
        } else {
          alert('Error deleting book: ' + data.error);
        }
      })
      .catch(error => {
        hideSpinner();
        alert('Error deleting book: ' + error.message);
      });
    }
  </script>
</body>
</html>
)=====";

  server.send(200, "text/html", html);
}

void setup ( void ) {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.begin(115200);
  server.collectHeaders(headersToCollect,1);
  delay(5000);

  SPI.begin();
  SD.begin();

  readConfig(SD);
  
  Serial.printf("Attempting to connect to %s\n", config["wifi_ssid"].c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(config["wifi_ssid"].c_str(), config["wifi_password"].c_str());
  Serial.println("");

  while ( WiFi.status() != WL_CONNECTED ) {
      delay ( 500 );
      Serial.print ( "." );
  }

  Serial.println ( "" );
  Serial.print ( "Connected to " );
  Serial.println ( config["wifi_ssid"] );
  Serial.print ( "IP address: " );
  Serial.println ( WiFi.localIP() );

  if (!MDNS.begin("library"))  {
    Serial.println("Error setting up MDNS responder!");
  } else {
    Serial.println("OK mDNS");
  }
  
  openDb(db_filename);

  server.on("/books", HTTP_GET, handleViewBooks);
  server.on("/logout", HTTP_GET, handleLogout);
  server.on("/authenticate", HTTP_POST, handleAuthenticate);
  server.on("/add", HTTP_POST, handleAddBook);
  server.on("/delete", HTTP_POST, handleDeleteBook);
  server.on("/edit", HTTP_POST, handleEditSubmit);
  server.on("/backup", HTTP_GET, handleBackup);
  server.on("/img", HTTP_GET, displayImageFiles);
  server.on("/favicon.ico", HTTP_GET, displayImageFiles);
  server.on("/", HTTP_GET, handleFrontend);

  server.onNotFound ( handleNotFound );
  server.begin();
  Serial.println ( "HTTP server started" );
}

void loop ( void ) {
  server.handleClient();
}
