/*
 * This file is part of the esp32 web interface
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include "clara_uart.h"
#include "driver/uart.h"

#define CLARA_PORT      UART_NUM_1
#define CLARA_RX_BUF    1024
#define CLARA_TX_BUF    256
#define PARTIAL_FLUSH   60    // мс тиші, після яких недописаний рядок віддаємо як є

namespace ClaraUart {

static char     ring[CLARA_LINES][CLARA_LINE_LEN];
static uint32_t head = 0;     // seq наступного рядка, який буде записано
static uint32_t baudRate = 0;
static bool     running = false;

// Незавершений рядок: Клара друкує запрошення без переводу каретки, тож тримаємо
// його окремо і зливаємо в кільце або по '\n', або по паузі в потоці.
static char     partial[CLARA_LINE_LEN];
static size_t   partialLen = 0;
static uint32_t partialSince = 0;

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// Черга подій драйвера: із неї видно помилки кадрування. Це єдиний спосіб
// відрізнити «лінія шумить або швидкість не та» від «дані справжні, просто
// двійкові»: у першому випадку лічильник біжить, у другому лишається нулем.
static QueueHandle_t evtQueue = NULL;
static uint32_t frameErrors = 0;
static uint32_t overruns = 0;

static void pushLine(const char* text) {
  char line[CLARA_LINE_LEN];
  uint32_t ms = millis();

  // Штамп часу тим самим форматом, що й у трейсах PCAN: секунди від старту.
  int n = snprintf(line, sizeof(line), "%4lu.%03lu ",
                   (unsigned long)(ms / 1000), (unsigned long)(ms % 1000));
  snprintf(line + n, sizeof(line) - n, "%s", text);

  taskENTER_CRITICAL(&mux);
  memcpy(ring[head % CLARA_LINES], line, CLARA_LINE_LEN);
  head++;
  taskEXIT_CRITICAL(&mux);
}

void Note(const char* fmt, ...) {
  char text[CLARA_LINE_LEN];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(text, sizeof(text), fmt, ap);
  va_end(ap);
  pushLine(text);
}

static void flushPartial() {
  if (partialLen == 0) return;
  partial[partialLen] = '\0';
  pushLine(partial);
  partialLen = 0;
}

void Init(int rxPin, int txPin, uint32_t baud) {
  if (running) {
    uart_driver_delete(CLARA_PORT);
    evtQueue = NULL;              // чергу знищує сам драйвер
    running = false;
  }

  if (baud == 0 || rxPin < 0) return;   // монітор вимкнено конфігом

  uart_config_t cfg = {};
  cfg.baud_rate  = (int)baud;
  cfg.data_bits  = UART_DATA_8_BITS;
  cfg.parity     = UART_PARITY_DISABLE;
  cfg.stop_bits  = UART_STOP_BITS_1;
  cfg.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;
  cfg.source_clk = UART_SCLK_DEFAULT;

  frameErrors = 0;
  overruns = 0;
  if (uart_driver_install(CLARA_PORT, CLARA_RX_BUF, CLARA_TX_BUF, 20, &evtQueue, 0) != ESP_OK)
    return;
  if (uart_param_config(CLARA_PORT, &cfg) != ESP_OK) {
    uart_driver_delete(CLARA_PORT);
    return;
  }
  // txPin < 0 — монітор «тільки слухаю»: лінія до Клари фізично не заведена.
  if (uart_set_pin(CLARA_PORT,
                   txPin < 0 ? UART_PIN_NO_CHANGE : txPin, rxPin,
                   UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
    uart_driver_delete(CLARA_PORT);
    return;
  }

  baudRate = baud;
  running = true;
  partialLen = 0;
  Note("UART%d %lu 8N1  RX=GPIO%d TX=%s", (int)CLARA_PORT, (unsigned long)baud,
       rxPin, txPin < 0 ? "-" : String(txPin).c_str());
}

void Loop() {
  if (!running) return;

  // Чергу подій треба вичерпувати, інакше вона заповнюється й драйвер починає
  // губити події. Заразом рахуємо биті кадри.
  uart_event_t ev;
  while (evtQueue && xQueueReceive(evtQueue, &ev, 0) == pdTRUE) {
    if (ev.type == UART_FRAME_ERR || ev.type == UART_PARITY_ERR) frameErrors++;
    else if (ev.type == UART_FIFO_OVF || ev.type == UART_BUFFER_FULL) overruns++;
  }

  uint8_t buf[128];
  int len = uart_read_bytes(CLARA_PORT, buf, sizeof(buf), 0);

  for (int i = 0; i < len; i++) {
    uint8_t c = buf[i];

    if (c == '\n') { flushPartial(); continue; }
    if (c == '\r') continue;
    if (c < 0x20 && c != '\t') continue;   // ESC-послідовності терміналу нам ні до чого

    if (partialLen == 0) partialSince = millis();
    partial[partialLen++] = (char)c;

    if (partialLen >= CLARA_LINE_LEN - 12) flushPartial();  // −12 під штамп часу
  }

  if (partialLen > 0 && (millis() - partialSince) > PARTIAL_FLUSH)
    flushPartial();
}

void Send(const char* cmd) {
  if (!running) return;
  size_t n = strlen(cmd);
  if (n) uart_write_bytes(CLARA_PORT, cmd, n);
  uart_write_bytes(CLARA_PORT, "\n", 1);
  Note("> %s", cmd);
}

void Clear() {
  taskENTER_CRITICAL(&mux);
  head = 0;
  taskEXIT_CRITICAL(&mux);
  partialLen = 0;
  frameErrors = 0;
  overruns = 0;
}

size_t Json(uint32_t since, char* out, size_t cap) {
  taskENTER_CRITICAL(&mux);
  uint32_t h = head;
  taskEXIT_CRITICAL(&mux);

  // Читач відстав більше, ніж уміщає кільце, або сторінку відкрили заново.
  uint32_t oldest = h > CLARA_LINES ? h - CLARA_LINES : 0;
  uint32_t lost = 0;
  if (since < oldest) { lost = oldest - since; since = oldest; }
  if (since > h) since = h;   // буфер чистили — читач попереду

  size_t pos = snprintf(out, cap,
                        "{\"head\":%lu,\"lost\":%lu,\"run\":%d,\"baud\":%lu,\"ferr\":%lu,\"ovf\":%lu,\"lines\":[",
                        (unsigned long)h, (unsigned long)lost, running ? 1 : 0, (unsigned long)baudRate,
                        (unsigned long)frameErrors, (unsigned long)overruns);

  for (uint32_t s = since; s < h && pos < cap - 8; s++) {
    if (s != since && pos < cap - 2) out[pos++] = ',';
    if (pos < cap - 2) out[pos++] = '"';

    taskENTER_CRITICAL(&mux);
    const char* src = ring[s % CLARA_LINES];
    char line[CLARA_LINE_LEN];
    memcpy(line, src, CLARA_LINE_LEN);
    taskEXIT_CRITICAL(&mux);
    line[CLARA_LINE_LEN - 1] = '\0';

    for (const char* p = line; *p && pos < cap - 8; p++) {
      if (*p == '"' || *p == '\\') out[pos++] = '\\';
      else if ((uint8_t)*p < 0x20) continue;
      out[pos++] = *p;
    }
    if (pos < cap - 2) out[pos++] = '"';
  }

  pos += snprintf(out + pos, cap - pos, "]}");
  return pos;
}

uint32_t GetBaud() { return baudRate; }
bool IsRunning() { return running; }

}
