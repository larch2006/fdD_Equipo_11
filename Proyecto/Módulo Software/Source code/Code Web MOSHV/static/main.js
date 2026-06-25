/* ──────────────────────────────────────────────
   main.js – Monitor ESP32
   ────────────────────────────────────────────── */

const SENSORES = ["ph", "tds", "temperatura_agua", "humedad"];

// Periodo activo por sensor (por defecto: hora)
const periodoActivo = { ph: "hora", tds: "hora", temperatura_agua: "hora", humedad: "hora" };

// Referencia a instancias de Chart.js
const charts = {};

// Colores de línea por sensor
const COLORS = {
  ph:               { line: "#0ea5e9", fill: "rgba(14,165,233,.12)" },
  tds:              { line: "#6366f1", fill: "rgba(99,102,241,.12)" },
  temperatura_agua: { line: "#22c55e", fill: "rgba(34,197,94,.12)"  },
  humedad:          { line: "#f59e0b", fill: "rgba(245,158,11,.12)" },
};

const ESTADO_LABELS = {
  optimo:        "Óptimo",
  margen:        "Al margen",
  critico:       "Fuera de rango",
  error:         "Error",
  sin_conexion:  "Sin conexión",
};

// ── INIT ──────────────────────────────────────

function initCharts() {
  SENSORES.forEach(sensor => {
    const ctx = document.getElementById(`chart-${sensor}`).getContext("2d");
    charts[sensor] = new Chart(ctx, {
      type: "line",
      data: {
        labels: [],
        datasets: [{
          label: sensor,
          data: [],
          borderColor:     COLORS[sensor].line,
          backgroundColor: COLORS[sensor].fill,
          borderWidth: 2,
          pointRadius: 3,
          pointHoverRadius: 5,
          tension: 0.4,
          fill: true,
        }]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        plugins: { legend: { display: false } },
        scales: {
          x: {
            ticks: {
              font: { size: 10 },
              maxTicksLimit: 8,
              maxRotation: 0,
            },
            grid: { color: "#f1f5f9" },
          },
          y: {
            ticks: { font: { size: 10 } },
            grid: { color: "#f1f5f9" },
          }
        }
      }
    });
  });
}

// ── ACTUALIZAR DATOS ACTUALES ─────────────────

async function actualizarDatos() {
  try {
    const res = await fetch("/api/datos");
    if (!res.ok) throw new Error("HTTP " + res.status);
    const d = await res.json();

    const enLinea = d.estado_sistema === "en_linea";

    // Estado sistema (pill)
    const dot  = document.getElementById("statusDot");
    const txt  = document.getElementById("statusText");
    dot.className = "status-dot " + (enLinea ? "online" : "offline");
    txt.textContent = enLinea ? "En línea" : "Sin conexión";

    // Aviso banner
    const banner = document.getElementById("alertBanner");
    banner.style.display = enLinea ? "none" : "block";

    // Última actualización
    if (d.fecha_hora) {
      document.getElementById("lastUpdate").textContent =
        formatFechaHora(d.fecha_hora);
    }

    // Temperatura ambiente
    const ta = d.temperatura_ambiente;
    document.getElementById("tempAmbiente").textContent =
      ta && ta.valor != null ? ta.valor.toFixed(1) : "—";

    // Tarjetas de sensores
    SENSORES.forEach(sensor => {
      const info = d[sensor] || {};
      const valEl   = document.getElementById(`val-${sensor}`);
      const badgeEl = document.getElementById(`badge-${sensor}`);

      valEl.textContent =
        info.valor != null ? formatValor(info.valor) : "—";

      const estado = info.estado || "error";
      badgeEl.textContent = ESTADO_LABELS[estado] || estado;
      badgeEl.className   = `estado-badge estado-${estado}`;
    });

  } catch (err) {
    console.error("Error al obtener /api/datos:", err);
    document.getElementById("statusDot").className = "status-dot offline";
    document.getElementById("statusText").textContent = "Sin conexión";
    document.getElementById("alertBanner").style.display = "block";
  }
}

// ── ACTUALIZAR GRÁFICO DE UN SENSOR ──────────

async function actualizarHistorial(sensor, periodo) {
  try {
    const res = await fetch(`/api/historial/${sensor}/${periodo}`);
    if (!res.ok) throw new Error("HTTP " + res.status);
    const filas = await res.json();

    const chart = charts[sensor];
    chart.data.labels  = filas.map(f => formatEje(f.fecha_hora, periodo));
    chart.data.datasets[0].data = filas.map(f => f.valor);
    chart.update();
  } catch (err) {
    console.error(`Error historial ${sensor}/${periodo}:`, err);
  }
}

async function actualizarTodosLosGraficos() {
  const promesas = SENSORES.map(s => actualizarHistorial(s, periodoActivo[s]));
  await Promise.all(promesas);
}

// ── FORMATEO ──────────────────────────────────

function formatValor(v) {
  return Number.isInteger(v) ? String(v) : parseFloat(v).toFixed(2);
}

function formatFechaHora(str) {
  // str: "2024-06-15 14:35:00"
  const d = new Date(str.replace(" ", "T"));
  return d.toLocaleString("es-PE", {
    day: "2-digit", month: "short",
    hour: "2-digit", minute: "2-digit"
  });
}

function formatEje(str, periodo) {
  const d = new Date(str.replace(" ", "T"));
  if (periodo === "hora") {
    return d.toLocaleTimeString("es-PE", { hour: "2-digit", minute: "2-digit" });
  }
  if (periodo === "dia") {
    return d.toLocaleTimeString("es-PE", { hour: "2-digit", minute: "2-digit" });
  }
  return d.toLocaleDateString("es-PE", { day: "2-digit", month: "short" });
}

// ── BOTONES DE PERIODO ────────────────────────

function setupPeriodButtons() {
  document.querySelectorAll(".period-btn").forEach(btn => {
    btn.addEventListener("click", () => {
      const sensor = btn.dataset.sensor;
      const period = btn.dataset.period;

      // Actualizar botones activos del sensor
      document.querySelectorAll(`.period-btn[data-sensor="${sensor}"]`)
        .forEach(b => b.classList.remove("active"));
      btn.classList.add("active");

      periodoActivo[sensor] = period;
      actualizarHistorial(sensor, period);
    });
  });
}

// ── MENU LATERAL (visual, sin cambio de ruta) ─

function setupNav() {
  document.querySelectorAll(".nav-item").forEach(item => {
    item.addEventListener("click", e => {
      e.preventDefault();
      document.querySelectorAll(".nav-item").forEach(i => i.classList.remove("active"));
      item.classList.add("active");
    });
  });
}

// ── CICLO PRINCIPAL ───────────────────────────

async function cicloCompleto() {
  await actualizarDatos();
  await actualizarTodosLosGraficos();
}

// ── ARRANQUE ──────────────────────────────────

document.addEventListener("DOMContentLoaded", () => {
  initCharts();
  setupPeriodButtons();
  setupNav();

  // Primera carga
  cicloCompleto();

  // Actualización automática cada 60 segundos
  setInterval(cicloCompleto, 60_000);
});
