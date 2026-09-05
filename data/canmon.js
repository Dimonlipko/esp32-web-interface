/*
 * This file is part of the esp32 web interface
 *
 * Клієнт монітора шини. Пристрій тримає кільце на 256 кадрів із наскрізною
 * нумерацією і віддає не більше 120 за запит, тому на завантаженій шині частина
 * кадрів неминуче пролітає повз — це чесно показано лічильником "пропущено".
 *
 * Два режими. "Стрічка" — як у candump, кожен кадр окремим рядком; годиться,
 * коли трафіку мало або звужено фільтром. "За ID" — як у справжньому
 * аналізаторі: один рядок на ідентифікатор із лічильником, періодом і останніми
 * даними; саме він лишається читабельним, коли шина забита.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

var canmon = {

	since: 0,
	timer: 0,
	busy: false,
	view: 'log',          // 'log' | 'byid'
	maxRows: 400,
	ids: {},              // id -> { count, lastMs, period, data, dlc, ext, rtr }
	lostTotal: 0,

	start: function()
	{
		if (canmon.timer) return;
		canmon.poll();
		canmon.timer = setInterval(canmon.poll, 400);
	},

	stop: function()
	{
		if (!canmon.timer) return;
		clearInterval(canmon.timer);
		canmon.timer = 0;
	},

	/** @brief вмикає/вимикає прийом усіх кадрів на пристрої */
	setEnabled: function(on)
	{
		canmon.post('on=' + (on ? '1' : '0'), function() {
			if (on) canmon.start(); else canmon.stop();
			canmon.syncButtons(on);
		});
	},

	syncButtons: function(on)
	{
		var b = document.getElementById('canmon-toggle');
		if (b) b.textContent = on ? 'Stop' : 'Start';
	},

	clear: function()
	{
		canmon.ids = {};
		canmon.lostTotal = 0;
		canmon.since = 0;
		var log = document.getElementById('canmon-log');
		if (log) log.innerHTML = '';
		var body = document.getElementById('canmon-body');
		if (body) body.innerHTML = '';
		canmon.post('clear=1');
	},

	/** @brief програмний фільтр на пристрої: (id & mask) == (filter & mask) */
	applyFilter: function()
	{
		var id = document.getElementById('canmon-id').value.trim() || '0';
		var mask = document.getElementById('canmon-mask').value.trim() || '0';
		canmon.post('id=' + encodeURIComponent(id) + '&mask=' + encodeURIComponent(mask));
		canmon.clear();
	},

	setView: function(v)
	{
		canmon.view = v;
		document.getElementById('canmon-log').hidden = (v !== 'log');
		document.getElementById('canmon-table').hidden = (v !== 'byid');
		document.getElementById('canmon-view-log').classList.toggle('active', v === 'log');
		document.getElementById('canmon-view-byid').classList.toggle('active', v === 'byid');
		if (v === 'byid') canmon.renderById();
	},

	post: function(body, done)
	{
		var xhr = new XMLHttpRequest();
		xhr.onreadystatechange = function() { if (xhr.readyState === 4 && done) done(); };
		xhr.open('POST', '/api/canmon', true);
		xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
		xhr.send(body);
	},

	poll: function()
	{
		if (canmon.busy) return;      // повільна мережа не має ставити запити в чергу
		canmon.busy = true;

		var xhr = new XMLHttpRequest();
		xhr.onreadystatechange = function()
		{
			if (xhr.readyState !== 4) return;
			canmon.busy = false;
			if (xhr.status !== 200) return;

			var r;
			try { r = JSON.parse(xhr.responseText); } catch (e) { return; }

			if (r.lost) canmon.lostTotal += r.lost;
			canmon.since = r.head;
			canmon.syncButtons(!!r.on);

			for (var i = 0; i < r.frames.length; i++) canmon.ingest(r.frames[i]);
			if (canmon.view === 'byid') canmon.renderById();
			canmon.renderStatus(r);
		};
		xhr.open('GET', '/api/canmon?since=' + canmon.since, true);
		xhr.send();
	},

	ingest: function(f)
	{
		// зведення за ID тримаємо завжди, щоб перемикання режиму не втрачало історію
		var e = canmon.ids[f.id];
		if (!e) e = canmon.ids[f.id] = { count: 0, lastMs: 0, period: 0 };
		if (e.count) {
			var dt = f.t - e.lastMs;
			// просте згладжування, щоб період не стрибав від джитера опитування
			e.period = e.period ? Math.round(e.period * 0.7 + dt * 0.3) : dt;
		}
		e.count++;
		e.lastMs = f.t;
		e.data = f.d;
		e.dlc = f.d ? (f.d.split(' ').length) : 0;
		e.ext = f.x;
		e.rtr = f.r;

		if (canmon.view === 'log') canmon.appendLog(f);
	},

	appendLog: function(f)
	{
		var log = document.getElementById('canmon-log');
		if (!log) return;
		var atBottom = (log.scrollHeight - log.scrollTop - log.clientHeight) < 40;

		var secs = (f.t / 1000).toFixed(3);
		var id = (f.x ? f.id.padStart(8, '0') : f.id.padStart(3, '0'));
		var div = document.createElement('div');
		div.textContent = secs.padStart(9) + '  ' + id.padEnd(8) +
		                  ' [' + (f.r ? 'R' : (f.d ? f.d.split(' ').length : 0)) + ']  ' + (f.d || '');
		log.appendChild(div);

		while (log.childNodes.length > canmon.maxRows) log.removeChild(log.firstChild);
		if (atBottom) log.scrollTop = log.scrollHeight;
	},

	renderById: function()
	{
		var body = document.getElementById('canmon-body');
		if (!body) return;

		var keys = Object.keys(canmon.ids).sort(function(a, b) {
			return parseInt(a, 16) - parseInt(b, 16);
		});

		body.innerHTML = '';
		for (var i = 0; i < keys.length; i++)
		{
			var k = keys[i], e = canmon.ids[k];
			var tr = body.insertRow(-1);
			tr.insertCell(-1).textContent = (e.ext ? k.padStart(8, '0') : k.padStart(3, '0'));
			tr.insertCell(-1).textContent = e.rtr ? 'RTR' : e.dlc;
			var d = tr.insertCell(-1);
			d.textContent = e.data || '';
			d.className = 'mono';
			tr.insertCell(-1).textContent = e.count;
			tr.insertCell(-1).textContent = e.period ? e.period + ' ms' : '';
		}
	},

	renderStatus: function(r)
	{
		var el = document.getElementById('canmon-status');
		if (!el) return;
		var ids = Object.keys(canmon.ids).length;
		el.textContent = (r.on ? 'приймаю все' : 'вимкнено') +
		                 ' · ' + ids + ' ID' +
		                 ' · кадрів ' + r.head +
		                 (canmon.lostTotal ? ' · пропущено ' + canmon.lostTotal : '');
	}
};
