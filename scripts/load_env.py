import json
import os
from pathlib import Path

Import("env")


PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))
ENV_FILE = PROJECT_DIR / ".env"
REQUIRED_KEYS = (
    "WIFI_SSID",
    "WIFI_PASSWORD",
    "MQTT_HOST",
    "MQTT_PORT",
    "MQTT_CLIENT_ID",
    "MQTT_USERNAME",
    "MQTT_PASSWORD",
    "MQTT_TELEMETRY_TOPIC",
)


def read_dotenv(path):
    """Read simple KEY=VALUE entries from a dotenv file."""
    values = {}
    if not path.exists():
        return values

    for line_number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("export "):
            line = line[7:].lstrip()
        if "=" not in line:
            raise RuntimeError(
                f"Error: {path.name}:{line_number}: invalid configuration line"
            )

        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in ("'", '"'):
            value = value[1:-1]
        values[key] = value

    return values


# Prefer the local .env file, then fall back to operating-system variables.
config = read_dotenv(ENV_FILE)
for key in REQUIRED_KEYS:
    if key not in config and key in os.environ:
        config[key] = os.environ[key]

missing = [key for key in REQUIRED_KEYS if not config.get(key)]
if missing:
    raise RuntimeError(
        "Error: Missing configuration: "
        + ", ".join(missing)
        + ". Copy .env.example to .env and enter the required values."
    )

try:
    mqtt_port = int(config["MQTT_PORT"])
except ValueError as error:
    raise RuntimeError("Error: MQTT_PORT must be an integer") from error

if not 1 <= mqtt_port <= 65535:
    raise RuntimeError("Error: MQTT_PORT must be between 1 and 65535")

# Generate a C++ header inside the build directory, keeping secrets out of source files.
generated_dir = Path(env.subst("$BUILD_DIR")) / "generated"
generated_dir.mkdir(parents=True, exist_ok=True)
generated_header = generated_dir / "env_config.h"

header_lines = [
    "#pragma once",
    "",
    f"#define WIFI_SSID {json.dumps(config['WIFI_SSID'])}",
    f"#define WIFI_PASSWORD {json.dumps(config['WIFI_PASSWORD'])}",
    f"#define MQTT_HOST {json.dumps(config['MQTT_HOST'])}",
    f"#define MQTT_PORT {mqtt_port}",
    f"#define MQTT_CLIENT_ID {json.dumps(config['MQTT_CLIENT_ID'])}",
    f"#define MQTT_USERNAME {json.dumps(config['MQTT_USERNAME'])}",
    f"#define MQTT_PASSWORD {json.dumps(config['MQTT_PASSWORD'])}",
    f"#define MQTT_TELEMETRY_TOPIC {json.dumps(config['MQTT_TELEMETRY_TOPIC'])}",
    "",
]
generated_header.write_text("\n".join(header_lines), encoding="utf-8")
# Add the generated directory to the compiler's header search path.
env.AppendUnique(CPPPATH=[str(generated_dir)])
