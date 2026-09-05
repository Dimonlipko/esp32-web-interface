/*
 * This file is part of the esp32 web interface
 *
 * Тема, підсвітка активної вкладки і статусні лампи у верхній панелі.
 *
 * Навмисно не чіпає ui.js: openPage і смуга помилки зв'язку загорнуті тут, тож
 * оригінальна логіка лишається такою, як в апстрімі.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

var theme = {

	apply: function(mode) {
		document.documentElement.setAttribute('data-theme', mode);
		try { localStorage.setItem('oi-theme', mode); } catch (e) { /* приватний режим */ }
	},

	toggle: function() {
		var cur = document.documentElement.getAttribute('data-theme');
		theme.apply(cur === 'dark' ? 'light' : 'dark');
	},

	/** @brief стан однієї лампи: '', 'ok', 'warn', 'error' */
	setLight: function(id, state, label) {
		var el = document.getElementById(id);
		if (!el) return;
		el.className = 'status-light' + (state ? ' ' + state : '');
		if (label) {
			var lab = document.getElementById(id + '-label');
			if (lab) lab.textContent = label;
		}
	},

	init: function() {
		var saved = null;
		try { saved = localStorage.getItem('oi-theme'); } catch (e) { /* ignore */ }
		if (saved) {
			document.documentElement.setAttribute('data-theme', saved);
		} else if (window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches) {
			document.documentElement.setAttribute('data-theme', 'dark');
		} else {
			document.documentElement.setAttribute('data-theme', 'light');
		}

		// --- підсвітка активної вкладки класом, а не інлайновим білим тлом ---
		if (window.ui && ui.openPage) {
			var origOpen = ui.openPage;
			ui.openPage = function(pageName, elmnt, color) {
				origOpen.call(ui, pageName, elmnt, '');
				var links = document.getElementsByClassName('tablink');
				for (var i = 0; i < links.length; i++) links[i].classList.remove('active');
				if (elmnt) elmnt.classList.add('active');

				// Термінал і монітор шини опитують пристрій, тож крутимо їх лише
				// на своїй вкладці.
				if (window.clara) {
					if (pageName === 'clara') clara.start(); else clara.stop();
				}
				if (window.canmon) {
					if (pageName === 'canmon') { if (canmon.timer) canmon.start(); }
					else canmon.stop();
				}
			};
		}

		// --- лампа Клари має бути чесною й поза своєю вкладкою ---------------
		// clara.poll() працює лише коли відкрито термінал, тож без цього лампа
		// показувала б стан, застиглий з моменту виходу з вкладки. Запит із
		// завідомо великим since сервер обрізає до head і повертає порожній
		// список — це кілька десятків байтів раз на 5 с.
		setInterval(function() {
			if (window.clara && clara.timer) return;   // вкладка відкрита, там свій опит
			var xhr = new XMLHttpRequest();
			xhr.onreadystatechange = function() {
				if (xhr.readyState !== 4) return;
				if (xhr.status !== 200) return theme.setLight('st-clara', 'error', 'Clara: no link');
				var r;
				try { r = JSON.parse(xhr.responseText); } catch (e) { return; }
				theme.setLight('st-clara', r.run ? 'ok' : '', r.run ? 'Clara UART' : 'Clara: off');
			};
			xhr.open('GET', '/api/term?since=4294967295', true);
			xhr.send();
		}, 5000);

		// --- згорнутий сайдбар ------------------------------------------------
		// ui.shrinkNavbar/growNavbar лишились з часів, коли іконки меню були
		// .buttonimg: вони й досі розганяють КОЖНУ .buttonimg до 60px завширшки,
		// не чіпаючи висоти. Тепер меню на inline-SVG (.navimg), тож єдиний
		// ефект — розплющені іконки на кнопках усередині сторінок. Заодно
		// growNavbar лупить display:block по .small-screen-hide, що збиває
		// flex-розкладку перемикача Auto reload. Прибираємо обидва інлайни.
		if (window.ui && ui.shrinkNavbar) {
			var origShrink = ui.shrinkNavbar, origGrow = ui.growNavbar;

			var clearButtonImgWidths = function() {
				var imgs = document.getElementsByClassName('buttonimg');
				for (var i = 0; i < imgs.length; i++) imgs[i].style.width = '';
			};

			ui.shrinkNavbar = function() {
				origShrink.call(ui);
				document.getElementById('navbar').classList.add('collapsed');
				clearButtonImgWidths();
			};
			ui.growNavbar = function() {
				origGrow.call(ui);
				document.getElementById('navbar').classList.remove('collapsed');
				clearButtonImgWidths();
				// display лишаємо на розсуд CSS: .control — блок, а
				// #auto-reload-toggle-div — flex із власним відступом.
				var items = document.getElementsByClassName('small-screen-hide');
				for (var i = 0; i < items.length; i++) items[i].style.display = '';
			};
		}

		// --- смуга помилки зв'язку дублюється лампою CAN ---------------------
		if (window.ui && ui.showCommunicationErrorBar) {
			var origShow = ui.showCommunicationErrorBar;
			var origHide = ui.hideCommunicationErrorBar;
			ui.showCommunicationErrorBar = function() {
				origShow.call(ui);
				theme.setLight('st-can', 'error', 'CAN: no reply');
			};
			ui.hideCommunicationErrorBar = function() {
				origHide.call(ui);
				theme.setLight('st-can', 'ok', 'CAN');
			};
		}
	}
};

document.addEventListener('DOMContentLoaded', theme.init);
