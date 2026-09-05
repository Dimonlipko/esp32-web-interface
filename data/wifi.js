var wifi = {

	/** @brief WPA2 needs at least 8 characters; an empty field means "leave it alone".
	 *  Upstream indexed #apsubmit unconditionally, but that element is commented out in
	 *  wifi.html, so every keystroke in the AP password field threw a TypeError. */
	wifiValidatePasswordLength: function(pw)
	{
		var btn = document.getElementById("apsubmit");
		if (!btn) return;
		btn.disabled = pw.length > 0 && pw.length < 8;
	},

	/** @brief called by wifi.html for the station form */
	wifiStationForm: function(formId)
	{
		ui.postForm(formId, 'WiFi settings');
	},

	/** @brief запускає пошук мереж і опитує ESP, доки скан не завершиться.
	 *  Сам скан на пристрої асинхронний, тому тут просте опитування. */
	scan: function()
	{
		var btn = document.getElementById('wifiScanBtn');
		if (btn) btn.disabled = true;
		wifi.scanStatus('Scanning\u2026');
		wifi.pollScan(0);
	},

	scanStatus: function(text, isError)
	{
		var box = document.getElementById('wifiScanResults');
		if (!box) return;
		box.innerHTML = '';
		var p = document.createElement('p');
		p.className = isError ? 'error' : 'clara-hint';
		p.textContent = text;
		box.appendChild(p);
	},

	pollScan: function(tries)
	{
		var xhr = new XMLHttpRequest();
		xhr.onload = function()
		{
			var r;
			try { r = JSON.parse(this.responseText); }
			catch (e) { return wifi.scanDone(null, 'unexpected reply from the ESP'); }

			if (r.state === 'scanning')
			{
				// скан на ESP32 займає 2–4 с; 20 спроб по 700 мс з великим запасом
				if (tries > 20) return wifi.scanDone(null, 'scan did not finish');
				setTimeout(function() { wifi.pollScan(tries + 1); }, 700);
				return;
			}
			wifi.scanDone(r.networks || [], null);
		};
		xhr.onerror = function() { wifi.scanDone(null, 'no reply from the ESP'); };
		xhr.open('GET', '/wifi/scan', true);
		xhr.send();
	},

	scanDone: function(networks, error)
	{
		var btn = document.getElementById('wifiScanBtn');
		if (btn) btn.disabled = false;

		if (error) return wifi.scanStatus(error, true);
		if (!networks.length) return wifi.scanStatus('No networks found');

		networks.sort(function(a, b) { return b.rssi - a.rssi; });

		var box = document.getElementById('wifiScanResults');
		box.innerHTML = '';
		var table = document.createElement('table');
		table.className = 'scan-table';

		for (var i = 0; i < networks.length; i++)
		{
			var n = networks[i];
			var tr = table.insertRow(-1);

			// SSID кладемо текстовою нодою, а не в innerHTML: ім'я мережі
			// приходить із ефіру й може містити що завгодно.
			var name = tr.insertCell(-1);
			var a = document.createElement('a');
			a.href = '#';
			a.textContent = n.ssid || '(hidden)';
			a.onclick = (function(ssid) {
				return function(e) {
					e.preventDefault();
					document.getElementById('staSSID').value = ssid;
					document.getElementById('staPW').focus();
					return false;
				};
			})(n.ssid);
			name.appendChild(a);

			tr.insertCell(-1).textContent = n.open ? 'open' : 'WPA';
			tr.insertCell(-1).textContent = n.rssi + ' dBm';
		}
		box.appendChild(table);
	},

	populateWiFiTab: function()
	{
		var wifiTab = document.getElementById("wifi");
		var wifiFetchRequest = new XMLHttpRequest();
		wifiFetchRequest.onload = function()
		{
			wifiTab.innerHTML = this.responseText;
		}
		wifiFetchRequest.open("GET", "/wifi");
		wifiFetchRequest.send();
	},

}