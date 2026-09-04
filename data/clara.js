/*
 * This file is part of the esp32 web interface
 *
 * Клієнт монітора UART Клари (ccs32clara).
 *
 * Пристрій тримає кільце на 128 рядків із наскрізною нумерацією; ми просимо все,
 * що новіше за останній побачений seq. Стану на сервері нема — вкладку можна
 * закрити й відкрити посеред потоку, а поле lost скаже, скільки рядків
 * пробігло повз, поки ми не питали.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

var clara = {

	since: 0,
	timer: 0,
	maxLines: 500,     // стільки рядків тримаємо в DOM, далі зрізаємо зверху
	busy: false,

	start: function() {
		if (clara.timer) return;
		clara.poll();
		clara.timer = setInterval(clara.poll, 500);
	},

	stop: function() {
		if (!clara.timer) return;
		clearInterval(clara.timer);
		clara.timer = 0;
	},

	append: function(text, cls) {
		var log = document.getElementById('clara-log');
		if (!log) return;

		// Автопрокрутка лише якщо користувач і так унизу — інакше він читає історію.
		var atBottom = (log.scrollHeight - log.scrollTop - log.clientHeight) < 40;

		var div = document.createElement('div');
		if (cls) div.className = cls;
		div.textContent = text;
		log.appendChild(div);

		while (log.childNodes.length > clara.maxLines) log.removeChild(log.firstChild);
		if (atBottom) log.scrollTop = log.scrollHeight;
	},

	poll: function() {
		if (clara.busy) return;            // повільна мережа не має ставити запити в чергу
		clara.busy = true;

		var xhr = new XMLHttpRequest();
		xhr.onreadystatechange = function() {
			if (xhr.readyState !== 4) return;
			clara.busy = false;

			if (xhr.status !== 200) {
				theme.setLight('st-clara', 'error', 'Clara: no link');
				return;
			}

			var r;
			try { r = JSON.parse(xhr.responseText); } catch (e) { return; }

			if (!r.run) {
				theme.setLight('st-clara', '', 'Clara: off');
			} else {
				theme.setLight('st-clara', r.lines.length ? 'warn' : 'ok', 'Clara UART');
			}

			if (r.lost > 0)
				clara.append('--- пропущено ' + r.lost + ' рядків ---', 'lost');

			for (var i = 0; i < r.lines.length; i++)
				clara.append(r.lines[i], r.lines[i].indexOf('> ') > 0 ? 'sent' : '');

			clara.since = r.head;
		};
		xhr.open('GET', '/api/term?since=' + clara.since, true);
		xhr.send();
	},

	send: function() {
		var input = document.getElementById('clara-input');
		var cmd = input.value;
		input.value = '';

		var xhr = new XMLHttpRequest();
		xhr.onreadystatechange = function() {
			if (xhr.readyState === 4 && xhr.status !== 200)
				clara.append('!!! ' + (xhr.responseText || 'send failed'), 'lost');
		};
		xhr.open('POST', '/api/term', true);
		xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
		xhr.send('cmd=' + encodeURIComponent(cmd));
	},

	clear: function() {
		var log = document.getElementById('clara-log');
		if (log) log.innerHTML = '';

		var xhr = new XMLHttpRequest();
		xhr.onreadystatechange = function() {
			if (xhr.readyState === 4) clara.since = 0;
		};
		xhr.open('POST', '/api/term', true);
		xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
		xhr.send('clear=1');
	},

	onKey: function(event) {
		if (event.keyCode === 13) clara.send();
	}
};
