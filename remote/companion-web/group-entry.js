import { createGroupApp } from './group.js';

const note = document.getElementById('connection-note');
const theme = document.getElementById('theme');
let currentTheme = matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
function showTheme(value) {
  currentTheme = value;
  document.documentElement.dataset.theme = value;
  theme.textContent = value === 'dark' ? 'Light theme' : 'Dark theme';
}
showTheme(currentTheme);
fetch('theme').then(r => r.json()).then(data => {
  if (['dark', 'light'].includes(data.theme)) showTheme(data.theme);
}).catch(() => {});
theme.addEventListener('click', async () => {
  showTheme(currentTheme === 'dark' ? 'light' : 'dark');
  try {
    const response = await fetch('theme', {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({theme: currentTheme})});
    if (!response.ok) throw new Error();
  } catch { note.textContent = 'Theme changed for this window. Could not save your preference.'; }
});
const app = createGroupApp(document.getElementById('group'), {
  assetURL: species => `assets/${species}.png`,
  send: async request => {
    const response = await fetch('actions', {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(request)});
    const data = await response.json();
    if (!response.ok) throw new Error(data.error || 'Could not send this action. Try again.');
  },
});
fetch('species').then(r => r.json()).then(data => app.updateSpecies(data)).catch(() => {});
let stopped = false;
async function refresh() {
  if (stopped) return;
  try {
    const response = await fetch('state');
    if (!response.ok) throw new Error();
    app.update(await response.json());
    note.textContent = '';
  } catch { app.setUnavailable('Connection interrupted. Reconnecting…'); note.textContent = 'Connection interrupted. Reconnecting…'; }
  if (!stopped) setTimeout(refresh, document.hidden ? 2000 : 500);
}
addEventListener('pagehide', () => { stopped = true; });
refresh();
