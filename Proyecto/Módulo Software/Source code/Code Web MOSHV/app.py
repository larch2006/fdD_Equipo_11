from flask import Flask, render_template, jsonify
import requests
import sqlite3
import os
from datetime import datetime
from apscheduler.schedulers.background import BackgroundScheduler

# Rutas absolutas para que Flask encuentre templates y static
# sin importar desde qué directorio se ejecute el script
BASE_DIR = os.path.dirname(os.path.abspath(__file__))

app = Flask(
    __name__,
    template_folder=os.path.join(BASE_DIR, "templates"),
    static_folder=os.path.join(BASE_DIR, "static"),
)

ESP32_URL = "http://192.168.1.35/api/sensores"
DB_PATH = os.path.join(BASE_DIR, "sensores.db")

# ──────────────────────────────────────────────
# BASE DE DATOS
# ──────────────────────────────────────────────

def init_db():
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    c.execute("""
        CREATE TABLE IF NOT EXISTS lecturas (
            id                      INTEGER PRIMARY KEY AUTOINCREMENT,
            fecha_hora              TEXT    NOT NULL,
            ph                      REAL,
            ph_estado               TEXT,
            tds                     REAL,
            tds_estado              TEXT,
            temperatura_agua        REAL,
            temperatura_agua_estado TEXT,
            humedad                 REAL,
            humedad_estado          TEXT,
            temperatura_ambiente    REAL
        )
    """)
    conn.commit()
    conn.close()


def guardar_lectura(datos):
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    c.execute("""
        INSERT INTO lecturas (
            fecha_hora, ph, ph_estado, tds, tds_estado,
            temperatura_agua, temperatura_agua_estado,
            humedad, humedad_estado, temperatura_ambiente
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    """, (
        datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        datos.get("ph", {}).get("valor"),
        datos.get("ph", {}).get("estado"),
        datos.get("tds", {}).get("valor"),
        datos.get("tds", {}).get("estado"),
        datos.get("temperatura_agua", {}).get("valor"),
        datos.get("temperatura_agua", {}).get("estado"),
        datos.get("humedad", {}).get("valor"),
        datos.get("humedad", {}).get("estado"),
        datos.get("temperatura_ambiente", {}).get("valor"),
    ))
    conn.commit()
    conn.close()

# ──────────────────────────────────────────────
# TAREA PROGRAMADA
# ──────────────────────────────────────────────

def consultar_esp32():
    try:
        r = requests.get(ESP32_URL, timeout=8)
        r.raise_for_status()
        datos = r.json()
        guardar_lectura(datos)
        print(f"[{datetime.now().strftime('%H:%M:%S')}] Lectura guardada OK")
    except Exception as e:
        print(f"[{datetime.now().strftime('%H:%M:%S')}] Error al consultar ESP32: {e}")
        # Guardar fila de error para que el frontend detecte el corte
        conn = sqlite3.connect(DB_PATH)
        c = conn.cursor()
        c.execute("""
            INSERT INTO lecturas (fecha_hora, ph_estado)
            VALUES (?, 'sin_conexion')
        """, (datetime.now().strftime("%Y-%m-%d %H:%M:%S"),))
        conn.commit()
        conn.close()

# ──────────────────────────────────────────────
# RUTAS FLASK
# ──────────────────────────────────────────────

@app.route("/")
def index():
    return render_template("index.html")


@app.route("/api/datos")
def api_datos():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    c = conn.cursor()
    c.execute("SELECT * FROM lecturas ORDER BY id DESC LIMIT 1")
    row = c.fetchone()
    conn.close()

    if not row:
        return jsonify({"estado_sistema": "sin_datos"})

    d = dict(row)
    en_linea = d.get("ph_estado") != "sin_conexion" and d.get("ph") is not None

    return jsonify({
        "estado_sistema": "en_linea" if en_linea else "sin_conexion",
        "fecha_hora": d["fecha_hora"],
        "ph":                 {"valor": d["ph"],               "estado": d["ph_estado"],               "unidad": "pH"},
        "tds":                {"valor": d["tds"],              "estado": d["tds_estado"],              "unidad": "ppm"},
        "temperatura_agua":   {"valor": d["temperatura_agua"], "estado": d["temperatura_agua_estado"], "unidad": "°C"},
        "humedad":            {"valor": d["humedad"],          "estado": d["humedad_estado"],          "unidad": "%"},
        "temperatura_ambiente":{"valor": d["temperatura_ambiente"],                                    "unidad": "°C"},
    })


PERIODO_SQL = {
    "hora":  "datetime('now', '-1 hour')",
    "dia":   "datetime('now', '-1 day')",
    "semana":"datetime('now', '-7 days')",
    "mes":   "datetime('now', '-30 days')",
}

SENSORES_VALIDOS = {"ph", "tds", "temperatura_agua", "humedad"}


@app.route("/api/historial/<sensor>/<periodo>")
def api_historial(sensor, periodo):
    if sensor not in SENSORES_VALIDOS:
        return jsonify({"error": "Sensor no válido"}), 400
    if periodo not in PERIODO_SQL:
        return jsonify({"error": "Periodo no válido"}), 400

    desde = PERIODO_SQL[periodo]
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    c = conn.cursor()
    # Usar LIMIT para evitar respuestas enormes
    c.execute(f"""
        SELECT fecha_hora, {sensor} AS valor
        FROM lecturas
        WHERE fecha_hora >= {desde}
          AND {sensor} IS NOT NULL
        ORDER BY fecha_hora ASC
        LIMIT 500
    """)
    filas = c.fetchall()
    conn.close()

    return jsonify([{"fecha_hora": f["fecha_hora"], "valor": f["valor"]} for f in filas])


# ──────────────────────────────────────────────
# INICIO
# ──────────────────────────────────────────────

if __name__ == "__main__":
    init_db()
    # Primera lectura al arrancar
    consultar_esp32()

    scheduler = BackgroundScheduler()
    scheduler.add_job(consultar_esp32, "interval", minutes=1)
    scheduler.start()

    try:
        app.run(debug=False, host="0.0.0.0", port=5000)
    finally:
        scheduler.shutdown()
