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