# DDLC OS Status

Live status page for the DDLC OS build, SD card test, image write and check, served at
<https://smartlab.electrosparkzz.in/>.

- The laptop runs `~/ddlc-os-status.py`, which reads the build and flash logs and publishes a retained JSON
  message every 3 s to `broker.hivemq.com`, topic `ddlc-os/afkar3374/status`. Progress only, no keys or passwords.
- `index.html` subscribes over `wss://broker.hivemq.com:8884/mqtt` (mqtt.js 5.10.1) and shows overall %,
  each step, and the recent log.
- `sw.js` only removes the old SMART LAB offline cache from phones that had installed it.

The previous SMART LAB dashboard is kept on the `smart-lab-archive` branch.
