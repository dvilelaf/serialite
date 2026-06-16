flash port="/dev/ttyACM0" version="latest":
    tools/flash-latest-firmware.sh --port {{port}} --version {{version}}

flash-dry-run port="/dev/ttyACM0" version="latest":
    tools/flash-latest-firmware.sh --port {{port}} --version {{version}} --dry-run
