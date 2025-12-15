import json
import time
import paho.mqtt.client as mqtt
import firebase_admin
from firebase_admin import credentials, db

#Config
MQTT_BROKER = "localhost"      # Mosquitto running on the Pi
MQTT_PORT = 1883

TELEMETRY_TOPIC = "plant/esp32_01/telemetry"
STATUS_TOPIC    = "plant/esp32_01/status"     

SERVICE_ACCOUNT_PATH = "serviceAccountKey.json"
DATABASE_URL = "https://YOUR-PROJECT-ID-default-rtdb.firebaseio.com/"  # <-- CHANGE THIS

# Firebase paths
TELEMETRY_PATH = "/plant/esp32_01/readings"
STATUS_PATH    = "/plant/esp32_01/status_log"

#Initialize firebase
cred = credentials.Certificate(SERVICE_ACCOUNT_PATH)
firebase_admin.initialize_app(cred, {"databaseURL": DATABASE_URL})

def safe_json_loads(raw: str):
    try:
        return json.loads(raw)
    except json.JSONDecodeError:
        return None

def on_connect(client, userdata, flags, rc):
    print("Connected to MQTT broker with code:", rc)
    client.subscribe(TELEMETRY_TOPIC)
    print("Subscribed to:", TELEMETRY_TOPIC)

    # Optional: subscribe to status too
    client.subscribe(STATUS_TOPIC)
    print("Subscribed to:", STATUS_TOPIC)

def on_message(client, userdata, msg):
    raw = msg.payload.decode("utf-8", errors="ignore").strip()
    print("MQTT:", msg.topic, raw)

    data = safe_json_loads(raw)
    if data is None:
        print("Skipping: payload is not valid JSON")
        return

    # Add server timestamp (epoch seconds)
    data["server_ts"] = int(time.time())

    
    if msg.topic == TELEMETRY_TOPIC:
        db.reference(TELEMETRY_PATH).push(data)
        print(f"Wrote telemetry to Firebase: {TELEMETRY_PATH}")

    elif msg.topic == STATUS_TOPIC:
        db.reference(STATUS_PATH).push(data)
        print(f"Wrote status to Firebase: {STATUS_PATH}")

    else:
        print("Skipping: unexpected topic")

def main():
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message

    client.connect(MQTT_BROKER, MQTT_PORT, 60)
    client.loop_forever()

if __name__ == "__main__":
    main()
