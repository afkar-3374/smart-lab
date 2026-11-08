# DDLC OS Tracker

Live tracking page for DDLC OS SD card flashes, served at <https://smartlab.electrosparkzz.in/>.
Enter the 3-digit tracking ID printed by `tools/ddlc-flash` (or open `?id=123`).

Source of truth: `tracker/index.html` in the private repo AI-DDLC/ddlc-os; copy it here to deploy.

- `ddlc-flash` publishes a retained JSON message every 3 s to `broker.hivemq.com`, topic
  `ddlc-os/track/<ID>`. Progress only, no keys or passwords.
- `index.html` subscribes over `wss://broker.hivemq.com:8884/mqtt` (mqtt.js 5.10.1) and shows overall %,
  each step, and the recent log.
- `sw.js` only removes the old SMART LAB offline cache from phones that had installed it.

The previous SMART LAB dashboard is kept on the `smart-lab-archive` branch.
