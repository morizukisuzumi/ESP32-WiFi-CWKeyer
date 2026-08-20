#include <WiFi.h>
#include <WebServer.h>

const char* ssid = "ssid";          // WiFi 名称
const char* password = "password"; // WiFi 密码

WebServer server(80);

String receivedMessage = "";
String lastMessage = "";
int wpm = 20;  // 默认 WPM
volatile bool stopRequested = false;

// ---------- 摩斯码表 ----------
struct MorseSymbol {
  char symbol;
  const char* code;
};

MorseSymbol morseTable[] = {
  // 字母
  {'A', ".-"}, {'B', "-..."}, {'C', "-.-."}, {'D', "-.."}, {'E', "."},
  {'F', "..-."}, {'G', "--."}, {'H', "...."}, {'I', ".."}, {'J', ".---"},
  {'K', "-.-"}, {'L', ".-.."}, {'M', "--"}, {'N', "-."}, {'O', "---"},
  {'P', ".--."}, {'Q', "--.-"}, {'R', ".-."}, {'S', "..."}, {'T', "-"},
  {'U', "..-"}, {'V', "...-"}, {'W', ".--"}, {'X', "-..-"}, {'Y', "-.--"},
  {'Z', "--.."},
  // 数字
  {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"}, {'4', "....-"},
  {'5', "....."}, {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."},
  // 常用标点
  {'.', ".-.-.-"}, {',', "--..--"}, {'?', "..--.."}, {'\'', ".----."},
  {'!', "-.-.--"}, {'/', "-..-."}, {'(', "-.--."}, {')', "-.--.-"},
  {'&', ".-..."}, {':', "---..."}, {';', "-.-.-."}, {'=', "-...-"},
  {'+', ".-.-."}, {'-', "-....-"}, {'_', "..--.-"}, {'"', ".-..-."},
  {'$', "...-..-"}, {'@', ".--.-."}
};

const char* getMorseCode(char c) {
  if (isAlpha(c)) c = toupper(c);
  for (int i = 0; i < sizeof(morseTable) / sizeof(morseTable[0]); i++) {
    if (morseTable[i].symbol == c) return morseTable[i].code;
  }
  return nullptr;
}
// -----------------------------

#define relay 13
#define buzzer 12

// 根据当前 WPM 计算时长
int dotDuration() {
  return 1200 / wpm;
}
int dashDuration() {
  return 3 * dotDuration();
}
int letterGapDuration() {
  return 3 * dotDuration();
}
int wordGapDuration() {
  return 7 * dotDuration();
}


void nonBlockingDelay(int ms) {
  unsigned long start = millis();
  while (millis() - start < (unsigned long)ms) {
    server.handleClient();
    if (stopRequested) return;
    delay(1);
  }
}

void playSymbol(char symbol) {
  int markDuration = (symbol == '-') ? dashDuration() : dotDuration();
  digitalWrite(relay, HIGH);
  digitalWrite(buzzer, HIGH);
  nonBlockingDelay(markDuration);
  digitalWrite(relay, LOW);
  digitalWrite(buzzer, LOW);
}

bool sendMorseChar(char c) {
  const char* code = getMorseCode(c);
  if (code == nullptr) return true;

  Serial.print(c);
  Serial.print(": ");
  for (int j = 0; code[j] != '\0'; j++) {
    if (stopRequested) return false;
    playSymbol(code[j]);
    if (stopRequested) return false;
    Serial.print(code[j]);
    if (code[j + 1] != '\0') {
      nonBlockingDelay(dotDuration());   // 符号间间隔
      if (stopRequested) return false;
    }
  }
  Serial.println();
  return true;
}

void sendMorseMessage(String msg) {
  Serial.println("Sending Morse: " + msg);
  stopRequested = false; // 新消息开始前清除停止标志

  for (int i = 0; i < msg.length(); i++) {
    char c = msg[i];

    if (stopRequested) {
      Serial.println("Stopped by user.");
      return;
    }

    if (c == ' ') {
      nonBlockingDelay(wordGapDuration());
      if (stopRequested) return;
      Serial.print(" / ");
    } else if (getMorseCode(c) != nullptr) {
      bool ok = sendMorseChar(c);
      if (!ok) return;
      if (i + 1 < msg.length() && msg[i + 1] != ' ') {
        nonBlockingDelay(letterGapDuration());
        if (stopRequested) return;
      }
    }
  }
  Serial.println();
}

// ---------- Web 处理 ----------
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>CW Keyer</title>
  <script>
    function loadSaved() {
      var list = JSON.parse(localStorage.getItem('cwmsg_list') || '[]');
      renderList(list);
    }

    function saveList(list) {
      localStorage.setItem('cwmsg_list', JSON.stringify(list));
    }

    function renderList(list) {
      var container = document.getElementById('savedList');
      container.innerHTML = '';
      if (list.length === 0) {
        container.innerHTML = '<p>no messages here</p>';
        return;
      }
      for (var i = 0; i < list.length; i++) {
        var div = document.createElement('div');
        div.style.marginBottom = '5px';
        var span = document.createElement('span');
        span.textContent = list[i];
        span.style.marginRight = '10px';
        var useBtn = document.createElement('button');
        useBtn.textContent = 'use';
        useBtn.onclick = (function(index) {
          return function() { useMsg(index); };
        })(i);
        var delBtn = document.createElement('button');
        delBtn.textContent = 'delete';
        delBtn.onclick = (function(index) {
          return function() { deleteMsg(index); };
        })(i);
        div.appendChild(span);
        div.appendChild(useBtn);
        div.appendChild(delBtn);
        container.appendChild(div);
      }
    }

    function addMsg() {
      var input = document.getElementById('newMsg');
      var msg = input.value.trim();
      if (msg === '') {
        alert('input something');
        return;
      }
      var list = JSON.parse(localStorage.getItem('cwmsg_list') || '[]');
      list.push(msg);
      saveList(list);
      renderList(list);
      input.value = '';
    }

    function deleteMsg(index) {
      var list = JSON.parse(localStorage.getItem('cwmsg_list') || '[]');
      list.splice(index, 1);
      saveList(list);
      renderList(list);
    }

    function useMsg(index) {
      var list = JSON.parse(localStorage.getItem('cwmsg_list') || '[]');
      if (index >= 0 && index < list.length) {
        document.getElementById('msg').value = list[index];
      }
    }

    function stopSending() {
      fetch('/stop', { method: 'POST' });
    }

    window.onload = function() {
      loadSaved();
    };
  </script>
</head>
<body>
  <h2>CW Keyer</h2>
  <form action="/send" method="POST">
    Message: <input type="text" id="msg" name="msg"><br><br>
    WPM: <input type="number" name="wpm" min="1" max="100" value=")rawliteral" + String(wpm) + R"rawliteral("><br><br>
    <input type="submit" value="send">
    <button type="button" onclick="stopSending()">stop sending</button>
  </form>

  <h3>saved messages</h3>
  <div>
    <input type="text" id="newMsg" placeholder="input messages">
    <button type="button" onclick="addMsg()">+</button>
  </div>
  <div id="savedList"></div>

  <p>last sent: )rawliteral" + receivedMessage + R"rawliteral(</p>
  <p>current WPM: )rawliteral" + String(wpm) + R"rawliteral(</p>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

void handleSend() {
  if (server.hasArg("wpm")) {
    int newWpm = server.arg("wpm").toInt();
    if (newWpm >= 1 && newWpm <= 100) {
      wpm = newWpm;
    }
  }

  if (server.hasArg("msg")) {
    receivedMessage = server.arg("msg");
    Serial.println("Received: " + receivedMessage + " | WPM: " + String(wpm));
  }

  stopRequested = false; // 新的发送请求清除停止标志
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleStop() {
  stopRequested = true;
  digitalWrite(relay, LOW);
  digitalWrite(buzzer, LOW);
  Serial.println("Stop requested via web");
  server.send(200, "text/plain", "Stopped");
}

// ---------- setup / loop ----------
void setup() {
  Serial.begin(115200);
  pinMode(relay, OUTPUT);
  pinMode(buzzer, OUTPUT);
  digitalWrite(relay, LOW);
  digitalWrite(buzzer, LOW);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nConnected! IP: " + WiFi.localIP().toString());
  Serial.println("Open this IP in a browser to send Morse code.");

  server.on("/", handleRoot);
  server.on("/send", HTTP_POST, handleSend);
  server.on("/stop", HTTP_POST, handleStop);
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();

  if (receivedMessage != lastMessage) {
    lastMessage = receivedMessage;
    if (receivedMessage.length() > 0) {
      sendMorseMessage(receivedMessage);
    }
  }

  delay(20);
}
