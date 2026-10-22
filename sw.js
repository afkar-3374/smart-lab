/*
  SMART LAB service worker

  - Pages and site files: network first, so updates show immediately;
    the cached copy is used when offline.
  - Libraries and fonts from CDNs: served from cache, refreshed in the background.
  - Live data (MQTT over WebSocket) never goes through here.
*/

const VERSION = 'smartlab-v4';

const SHELL = [
  './',
  './circuit/',
  './code/',
  './manifest.webmanifest',
  './assets/site.css',
  './assets/pwa.js',
  './assets/icons/icon.svg',
  './assets/icons/icon-192.png',
  './assets/icons/icon-512.png',
  './assets/icons/apple-touch-icon.png',
  './assets/icons/favicon-32.png',
  './firmware/smart_lab_mqtt/smart_lab_mqtt.ino',
  './firmware/smart_lab_mqtt/secrets.example.h'
];

self.addEventListener('install', event => {
  event.waitUntil(
    caches.open(VERSION)
      .then(cache => cache.addAll(SHELL))
      .then(() => self.skipWaiting())
  );
});

self.addEventListener('activate', event => {
  event.waitUntil(
    caches.keys()
      .then(keys => Promise.all(keys.filter(k => k !== VERSION).map(k => caches.delete(k))))
      .then(() => self.clients.claim())
  );
});

async function networkFirst(request) {
  const cache = await caches.open(VERSION);
  try {
    const response = await fetch(request);
    if (response.ok) cache.put(request, response.clone());
    return response;
  } catch (err) {
    const cached = await cache.match(request, { ignoreSearch: true });
    if (cached) return cached;
    if (request.mode === 'navigate') return cache.match('./');
    throw err;
  }
}

async function staleWhileRevalidate(request) {
  const cache = await caches.open(VERSION);
  const cached = await cache.match(request);
  const refresh = fetch(request)
    .then(response => {
      if (response.ok || response.type === 'opaque') cache.put(request, response.clone());
      return response;
    })
    .catch(() => cached);
  return cached || refresh;
}

self.addEventListener('fetch', event => {
  const request = event.request;
  if (request.method !== 'GET' || !request.url.startsWith('http')) return;

  const sameOrigin = new URL(request.url).origin === self.location.origin;
  event.respondWith(sameOrigin ? networkFirst(request) : staleWhileRevalidate(request));
});

// Tapping an alarm notification opens (or focuses) the dashboard.
self.addEventListener('notificationclick', event => {
  event.notification.close();
  event.waitUntil(
    self.clients.matchAll({ type: 'window', includeUncontrolled: true }).then(list => {
      const open = list.find(c => c.url.startsWith(self.registration.scope));
      if (open) return open.focus();
      return self.clients.openWindow(self.registration.scope);
    })
  );
});
