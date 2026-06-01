#!/usr/bin/env bash
set -euo pipefail

# Wrapper for the YAML automation generator.
# Usage: bash scripts/create_ha_automation.sh

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
exec python3 "$SCRIPT_DIR/scripts/create_ha_automations.py" --yaml "$@"
