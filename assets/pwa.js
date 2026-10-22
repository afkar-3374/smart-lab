/*
  Shared app setup for every SMART LAB page:
  - registers the service worker (offline support, installable app)
  - shows the "Install" button only when the app is NOT installed
*/

(function () {
  'use strict';

  // The service worker lives at the site root; pages in sub-folders point one level up.
  const root = document.documentElement.dataset.root || './';
  const INSTALLED_KEY = 'smartlab-installed';

  if ('serviceWorker' in navigator) {
    window.addEventListener('load', () => {
      navigator.serviceWorker.register(root + 'sw.js').catch(() => {});
    });
  }

  let installEvent = null;

  function remember(installed) {
    try {
      if (installed) localStorage.setItem(INSTALLED_KEY, '1');
      else localStorage.removeItem(INSTALLED_KEY);
    } catch (e) { /* storage unavailable */ }
  }

  function rememberedInstalled() {
    try { return localStorage.getItem(INSTALLED_KEY) === '1'; } catch (e) { return false; }
  }

  // Running as the installed app (Android, desktop, or iPhone home screen).
  function runningAsApp() {
    return window.matchMedia('(display-mode: standalone)').matches ||
           window.matchMedia('(display-mode: window-controls-overlay)').matches ||
           window.navigator.standalone === true;
  }

  function setVisible(show) {
    document.querySelectorAll('[data-install]').forEach(b => { b.hidden = !show; });
  }

  // Hidden until the browser confirms the app can be installed.
  setVisible(false);
  if (runningAsApp()) remember(true);

  window.addEventListener('beforeinstallprompt', async event => {
    event.preventDefault();
    if (runningAsApp()) return;

    // Chrome on Android can report an installed copy of this app.
    if (navigator.getInstalledRelatedApps) {
      try {
        const apps = await navigator.getInstalledRelatedApps();
        if (apps.length) { remember(true); return; }
      } catch (e) { /* not supported */ }
    }

    // The browser only fires this event when the app is not installed,
    // so an old "installed" note is out of date (the app was removed).
    remember(false);
    installEvent = event;
    setVisible(true);
  });

  window.addEventListener('appinstalled', () => {
    installEvent = null;
    remember(true);
    setVisible(false);
  });

  document.addEventListener('click', async event => {
    const button = event.target.closest('[data-install]');
    if (!button || !installEvent) return;
    installEvent.prompt();
    const choice = await installEvent.userChoice;
    installEvent = null;
    setVisible(false);
    if (choice && choice.outcome === 'accepted') remember(true);
  });

  // Exposed for pages that want to know (not used for the button itself).
  window.smartlabInstalled = () => runningAsApp() || rememberedInstalled();
})();
