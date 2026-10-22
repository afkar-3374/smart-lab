/*
  Shared app setup for every SMART LAB page:
  - registers the service worker (offline support, installable app)
  - shows the "Install" button when the browser offers installation
*/

(function () {
  'use strict';

  // The service worker lives at the site root; pages in sub-folders point one level up.
  const root = document.documentElement.dataset.root || './';

  if ('serviceWorker' in navigator) {
    window.addEventListener('load', () => {
      navigator.serviceWorker.register(root + 'sw.js').catch(() => {});
    });
  }

  let installEvent = null;

  function installButtons() {
    return document.querySelectorAll('[data-install]');
  }

  window.addEventListener('beforeinstallprompt', event => {
    event.preventDefault();
    installEvent = event;
    installButtons().forEach(b => { b.hidden = false; });
  });

  window.addEventListener('appinstalled', () => {
    installEvent = null;
    installButtons().forEach(b => { b.hidden = true; });
  });

  document.addEventListener('click', async event => {
    const button = event.target.closest('[data-install]');
    if (!button || !installEvent) return;
    installEvent.prompt();
    await installEvent.userChoice;
    installEvent = null;
    installButtons().forEach(b => { b.hidden = true; });
  });
})();
