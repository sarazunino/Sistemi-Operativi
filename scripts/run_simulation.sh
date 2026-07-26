#!/bin/bash
# Script di comodo per lanciare la simulazione con un dato config
# Uso: ./scripts/run_simulation.sh config/config.conf
set -e

echo "Pulisco IPC rimaste da esecuzioni precedenti..."

ipcs -m | awk -v user="$USER" '$3==user {print $2}' | xargs -r -n1 ipcrm -m
ipcs -s | awk -v user="$USER" '$3==user {print $2}' | xargs -r -n1 ipcrm -s
ipcs -q | awk -v user="$USER" '$3==user {print $2}' | xargs -r -n1 ipcrm -q

echo "IPC pulite."

CONFIG=${1:-config/config.conf}

echo "Avvio simulazione con $CONFIG..."

./bin/responsabile_mensa "$CONFIG"
