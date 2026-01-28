#!/bin/bash

# Define the CLI tool command
CLI_TOOL="ninja -C build scc-daemon"

# Define the patterns to watch for
PATTERNS=("Controller removed:" "OSDD         Connection to daemon closed")

# Function to start the CLI tool and watch its output
start_and_watch() {
    # Start the CLI tool and capture its output
    $CLI_TOOL | while IFS= read -r line; do
        echo "$line"  # Optionally log the output
        for pattern in "${PATTERNS[@]}"; do
            if [[ "$line" == *"$pattern"* ]]; then
                echo "Detected pattern: '$pattern'. Restarting CLI tool..."
                $CLI_TOOL  # Restart the CLI tool
                return  # Exit the function after restarting
            fi
        done
    done
}

# Start the initial watch
start_and_watch
