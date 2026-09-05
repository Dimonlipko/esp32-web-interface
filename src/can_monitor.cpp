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
#include "can_monitor.h"

namespace CanMonitor {

typedef struct {
  uint32_t ms;
  uint32_t id;
  uint8_t  dlc;
  uint8_t  flags;      // біт0 — extended, біт1 — RTR
  uint8_t  data[8];
} Frame;

#define FLAG_EXT 0x01
#define FLAG_RTR 0x02

static Frame    ring[CANMON_FRAMES];
static uint32_t head = 0;         // seq наступного кадру, який буде записано
static bool     enabled = false;
static uint32_t filterId = 0;
static uint32_t filterMask = 0;

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

void Push(const twai_message_t* f) {
  if (!enabled) return;
  if (filterMask && ((f->identifier & filterMask) != (filterId & filterMask))) return;

  Frame e;
  e.ms = millis();
  e.id = f->identifier;
  e.dlc = f->data_length_code > 8 ? 8 : f->data_length_code;
  e.flags = (f->extd ? FLAG_EXT : 0) | (f->rtr ? FLAG_RTR : 0);
  memcpy(e.data, f->data, 8);

  taskENTER_CRITICAL(&mux);
  ring[head % CANMON_FRAMES] = e;
  head++;
  taskEXIT_CRITICAL(&mux);
}

void Clear() {
  taskENTER_CRITICAL(&mux);
  head = 0;
  taskEXIT_CRITICAL(&mux);
}

bool IsEnabled() { return enabled; }

void SetEnabled(bool on) {
  if (on == enabled) return;
  enabled = on;
  if (!on) Clear();
}

void SetFilter(uint32_t id, uint32_t mask) {
  filterId = id;
  filterMask = mask;
}

uint32_t GetFilterId() { return filterId; }
uint32_t GetFilterMask() { return filterMask; }

static const char HEXDIGIT[] = "0123456789ABCDEF";

size_t Json(uint32_t since, char* out, size_t cap) {
  taskENTER_CRITICAL(&mux);
  uint32_t h = head;
  taskEXIT_CRITICAL(&mux);

  // Скільки кадрів читач проґавив: або кільце обернулось, або сторінку відкрили
  // заново. drops — те, що взагалі не вмістилось у відповідь цього разу.
  uint32_t oldest = h > CANMON_FRAMES ? h - CANMON_FRAMES : 0;
  uint32_t lost = 0;
  if (since < oldest) { lost = oldest - since; since = oldest; }
  if (since > h) since = h;

  uint32_t drops = 0;
  if (h - since > CANMON_MAX_REPLY) {
    drops = (h - since) - CANMON_MAX_REPLY;
    since = h - CANMON_MAX_REPLY;
    lost += drops;
  }

  size_t pos = snprintf(out, cap, "{\"head\":%lu,\"lost\":%lu,\"on\":%d,\"frames\":[",
                        (unsigned long)h, (unsigned long)lost, enabled ? 1 : 0);

  for (uint32_t s = since; s < h && pos + 96 < cap; s++) {
    taskENTER_CRITICAL(&mux);
    Frame e = ring[s % CANMON_FRAMES];
    taskEXIT_CRITICAL(&mux);

    if (s != since) out[pos++] = ',';
    pos += snprintf(out + pos, cap - pos, "{\"t\":%lu,\"id\":\"%lX\",\"x\":%d,\"r\":%d,\"d\":\"",
                    (unsigned long)e.ms, (unsigned long)e.id,
                    (e.flags & FLAG_EXT) ? 1 : 0, (e.flags & FLAG_RTR) ? 1 : 0);

    for (uint8_t i = 0; i < e.dlc && pos + 4 < cap; i++) {
      if (i) out[pos++] = ' ';
      out[pos++] = HEXDIGIT[e.data[i] >> 4];
      out[pos++] = HEXDIGIT[e.data[i] & 0x0F];
    }
    pos += snprintf(out + pos, cap - pos, "\"}");
  }

  pos += snprintf(out + pos, cap - pos, "]}");
  return pos;
}

}
