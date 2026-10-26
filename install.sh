#!/bin/sh

INSTALL_DIR="$HOME/.local/bin"
MODEL_DIR="$HOME/.local/share/models"

MODEL_URL="https://huggingface.co/BAAI/bge-small-en-v1.5-gguf/resolve/main/bge-small-en-v1.5-f16.gguf"
MODEL_FILE="bge-small-en-v1.5.gguf"

mkdir -p "$INSTALL_DIR" "$MODEL_DIR"
BINARY_URL="https://codeberg.org/Enji/vex-rag/releases/download/latest/vex-rag"

curl -L --progress-bar -o "$INSTALL_DIR/vex-rag" "$BINARY_URL"

[ ! -f "$MODEL_DIR/$MODEL_FILE" ] && {
    curl -L --progress-bar -o "$MODEL_DIR/$MODEL_FILE" "$MODEL_URL"
}

echo ""
echo "Successfully installed!"
echo "  - Binary: $INSTALL_DIR/vex-rag"
echo "  - Model:  $MODEL_DIR/$MODEL_FILE"
echo "Make sure $INSTALL_DIR is in your \$PATH."