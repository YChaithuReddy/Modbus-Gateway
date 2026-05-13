#!/bin/bash
# Block edits to partition table and sensitive config files

# Read JSON input from stdin
INPUT=$(cat)

# Extract file path from tool input
FILE_PATH=$(echo "$INPUT" | jq -r '.tool_input.file_path // empty')

# Block partition table edits (FROZEN since v1.3.7)
if [[ "$FILE_PATH" == *"partitions"* ]] && [[ "$FILE_PATH" == *".csv"* ]]; then
  cat << 'EOF'
{
  "hookSpecificOutput": {
    "hookEventName": "PreToolUse",
    "permissionDecision": "deny",
    "permissionDecisionReason": "PARTITION TABLE IS FROZEN since v1.3.7. OTA updates cannot change partition layout. All deployed devices would need manual reflash. DO NOT modify partitions_4mb.csv."
  }
}
EOF
  exit 0
fi

# Block .env and credential files
if [[ "$FILE_PATH" == *".env"* ]] || [[ "$FILE_PATH" == *"credentials"* ]] || [[ "$FILE_PATH" == *"secret"* ]]; then
  cat << 'EOF'
{
  "hookSpecificOutput": {
    "hookEventName": "PreToolUse",
    "permissionDecision": "deny",
    "permissionDecisionReason": "Cannot edit credential files directly. Ask the user to edit manually."
  }
}
EOF
  exit 0
fi

# Allow other files
exit 0
