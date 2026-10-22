#!/bin/sh
# Installs the build tools and the PS5 payload SDK on macOS (Homebrew).
#
# Homebrew ships ld.lld in its own formula, outside the directory
# `llvm-config --bindir` names, where the SDK looks for every tool. So the
# tools are linked into one directory, with an llvm-config that names it,
# and the SDK finds that one through LLVM_CONFIG.
set -e
SDK_VERSION=v0.45
SDK_DIR="$HOME/.local/ps5-payload-sdk"
BIN="$HOME/.local/ps5-llvm/bin"

brew install llvm lld
LLVM_BIN="$(brew --prefix llvm)/bin"
LLD_BIN="$(brew --prefix lld)/bin"

mkdir -p "$BIN"
for t in clang clang++ clang-cpp llvm-ar llvm-nm llvm-objcopy llvm-ranlib llvm-strip; do
    ln -sf "$LLVM_BIN/$t" "$BIN/$t"
done
ln -sf "$LLD_BIN/ld.lld" "$BIN/ld.lld"
cat > "$BIN/llvm-config" <<EOF
#!/bin/sh
[ "\$1" = "--bindir" ] && { echo "$BIN"; exit 0; }
exec "$LLVM_BIN/llvm-config" "\$@"
EOF
chmod +x "$BIN/llvm-config"

if [ ! -d "$SDK_DIR" ]; then
    mkdir -p "$HOME/.local"
    cd /tmp
    curl -sSL -o ps5-payload-sdk.zip \
        "https://github.com/ps5-payload-dev/sdk/releases/download/$SDK_VERSION/ps5-payload-sdk.zip"
    unzip -q -o ps5-payload-sdk.zip -d "$HOME/.local"
    rm ps5-payload-sdk.zip
fi

cat > "$HOME/.ps5sdk" <<EOF
export PS5_PAYLOAD_SDK="$SDK_DIR"
export LLVM_CONFIG="$BIN/llvm-config"
EOF
echo "ok: . ~/.ps5sdk && make"
