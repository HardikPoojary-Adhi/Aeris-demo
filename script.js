const FIRESTORE_URL = "https://firestore.googleapis.com/v1/projects/aeris-8af63/databases/(default)/documents/devices/device_01";
const REFRESH_INTERVAL = 5000;
const $ = (selector) => document.querySelector(selector);
const fieldValue = (field) => { if (!field) return null; if (field.integerValue !== undefined) return Number(field.integerValue); if (field.doubleValue !== undefined) return Number(field.doubleValue); if (field.stringValue !== undefined) return field.stringValue; if (field.booleanValue !== undefined) return field.booleanValue; return null; };
const AQI_CATEGORIES = [{ max: 50, label: "GOOD", color: "#4ad991" }, { max: 100, label: "MODERATE", color: "#e8c94a" }, { max: 150, label: "UNHEALTHY FOR SENSITIVE GROUPS", color: "#f0973d" }, { max: 200, label: "UNHEALTHY", color: "#ec5b52" }, { max: 300, label: "HAZARDOUS", color: "#ec5b52" }];
function classifyAQI(value) { return AQI_CATEGORIES.find((category) => value <= category.max) || AQI_CATEGORIES[AQI_CATEGORIES.length - 1]; }
function formatValue(value, decimals = 1) { return value === null || value === undefined || Number.isNaN(Number(value)) ? "--" : Number(value).toFixed(decimals); }
function setText(selector, value, fallback = "--") { document.querySelectorAll(selector).forEach((element) => { element.textContent = value === null || value === undefined || value === "" ? fallback : value; }); }
function renderGauge(container, value) {
  const aqi = Math.max(0, Math.min(Number(value) || 0, 300)), category = classifyAQI(aqi), width = 240, height = 150, cx = 120, cy = 130, radius = 96, angle = Math.PI - (aqi / 300) * Math.PI;
  const point = (theta, r = radius) => ({ x: cx + r * Math.cos(theta), y: cy - r * Math.sin(theta) });
  const needle = point(angle, 78);
  const ticks = [50, 100, 150, 200, 300].map((tick) => { const theta = Math.PI - (tick / 300) * Math.PI, outer = point(theta, 106), inner = point(theta, 88); return `<line x1="${outer.x}" y1="${outer.y}" x2="${inner.x}" y2="${inner.y}" stroke="${category.color}" stroke-opacity=".7" stroke-width="2"/>`; }).join("");
  const gaugeTrack = `<path d="M 24 130 A 96 96 0 0 1 216 130" fill="none" stroke="${category.color}" stroke-opacity=".24" stroke-width="14"/>`;
  const activeEnd = point(angle);
  const activeArc = aqi > 0
    ? `<path d="M 24 130 A 96 96 0 0 1 ${activeEnd.x} ${activeEnd.y}" fill="none" stroke="${category.color}" stroke-width="14" stroke-linecap="round"/>`
    : "";
  container.innerHTML = `<svg viewBox="0 0 ${width} ${height}" role="img" aria-label="AQI ${formatValue(aqi, 0)}">${gaugeTrack}${activeArc}${ticks}<line x1="120" y1="130" x2="${needle.x}" y2="${needle.y}" stroke="${category.color}" stroke-width="3" stroke-linecap="round"/><circle cx="120" cy="130" r="6" fill="${category.color}"/><text x="120" y="147" text-anchor="middle" fill="${category.color}" font-size="9" font-family="JetBrains Mono, monospace">AQI / 300</text></svg>`;
}
function updateDashboard(fields) {
  const values = Object.fromEntries(Object.entries(fields).map(([key, value]) => [key, fieldValue(value)])), aqi = Number(values.aqi) || 0, category = classifyAQI(aqi);
  document.querySelectorAll("[data-field]").forEach((element) => { element.textContent = formatValue(values[element.dataset.field]); });
  setText("[data-aqi]", formatValue(aqi, 0)); setText("[data-aqi-label]", category.label); setText("[data-status]", values.status || "Online"); setText("[data-status-detail]", values.status || "Online"); setText("[data-device-time]", values.last_updated); setText("[data-last-updated]", values.last_updated);
  const status = $("[data-aqi-status]"); if (status) { status.style.color = category.color; status.style.backgroundColor = `${category.color}20`; status.querySelector(".hero-status-dot").style.backgroundColor = category.color; }
  renderGauge($("[data-gauge]"), aqi);
}
async function loadDevice() { try { const response = await fetch(FIRESTORE_URL, { cache: "no-store" }); if (!response.ok) throw new Error(`Firestore request failed: ${response.status}`); const documentData = await response.json(); updateDashboard(documentData.fields || {}); } catch (error) { console.error("Unable to load Firestore device data", error); setText("[data-status]", "Offline"); setText("[data-status-detail]", "Unavailable"); } }
function updateClock() { setText("[data-live-clock]", new Date().toLocaleTimeString("en-IN", { hour: "2-digit", minute: "2-digit", second: "2-digit", hour12: false })); }
document.addEventListener("DOMContentLoaded", () => { updateClock(); setInterval(updateClock, 1000); loadDevice(); setInterval(loadDevice, REFRESH_INTERVAL); window.addEventListener("scroll", () => $("#mainNav")?.classList.toggle("is-scrolled", window.scrollY > 12), { passive: true }); });