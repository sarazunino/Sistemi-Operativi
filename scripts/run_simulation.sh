#!/bin/bash
# Script di comodo per lanciare la simulazione con un dato config
# Uso: ./scripts/run_simulation.sh config/config.conf
set -e
CONFIG=${1:-config/config.conf}
./bin/responsabile_mensa "$CONFIG"
