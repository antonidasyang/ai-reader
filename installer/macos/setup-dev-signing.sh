#!/bin/sh
# One-time (idempotent) setup of a LOCAL dev code-signing identity.
#
# Why not just use the Developer ID: codesign can only reach a login-keychain
# private key from an Aqua (GUI) session. Builds driven from an ssh shell, a
# background agent, or CI get errSecInternalComponent and silently fall back to
# ad-hoc — and an ad-hoc signature changes every build, so the keychain ACL
# never matches and the app re-prompts for every secret it touches, forever.
#
# So the dev bundle gets its own self-signed identity in its own keychain whose
# password we own. It signs from ANY session type with no prompt, and its
# designated requirement (certificate root = <this cert>) is stable across
# rebuilds, which is the only property the keychain ACL cares about.
#
# This identity is deliberately NOT trusted and NOT for distribution — the
# shipped .dmg is still Developer ID + notarized via make-dmg.sh.
set -e

DIR="$HOME/.ai-reader-signing"
KC="$HOME/Library/Keychains/ai-reader-dev.keychain-db"
CN="AI Reader Dev Signing"
PW=aireaderdev   # guards an untrusted local-only cert; not a secret worth hiding

if [ "$(uname)" != "Darwin" ]; then
  echo "[setup-dev-signing] macOS only"; exit 0
fi

if security find-identity -p codesigning "$KC" 2>/dev/null | grep -q "$CN"; then
  echo "[setup-dev-signing] identity already present in $KC"
  exit 0
fi

mkdir -p "$DIR"; chmod 700 "$DIR"
printf '%s\n' "$PW" > "$DIR/keychain-password"; chmod 600 "$DIR/keychain-password"

# CA:false + critical codeSigning EKU are what make codesign accept the cert.
cat > "$DIR/openssl.cnf" <<'CNF'
[ req ]
distinguished_name = dn
x509_extensions    = v3
prompt             = no
[ dn ]
CN = AI Reader Dev Signing
O  = ai-reader local dev
[ v3 ]
basicConstraints     = critical,CA:false
keyUsage             = critical,digitalSignature
extendedKeyUsage     = critical,codeSigning
subjectKeyIdentifier = hash
CNF

openssl req -x509 -newkey rsa:2048 -sha256 -days 7300 -nodes \
  -keyout "$DIR/dev-signing.key" -out "$DIR/dev-signing.crt" \
  -config "$DIR/openssl.cnf" 2>/dev/null
openssl pkcs12 -export -inkey "$DIR/dev-signing.key" -in "$DIR/dev-signing.crt" \
  -name "$CN" -out "$DIR/dev-signing.p12" -passout "pass:$PW" 2>/dev/null
chmod 600 "$DIR/dev-signing.key" "$DIR/dev-signing.p12"

security create-keychain -p "$PW" "$KC"
security set-keychain-settings "$KC"          # no auto-lock, no timeout
security unlock-keychain -p "$PW" "$KC"
security import "$DIR/dev-signing.p12" -k "$KC" -P "$PW" -T /usr/bin/codesign -f pkcs12 -A
# Let codesign use the key without a UI prompt.
security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$PW" "$KC" >/dev/null 2>&1
# Append to the search list (keep whatever was already there).
security list-keychains -d user -s $(security list-keychains -d user | tr -d ' "') "$KC"

echo "[setup-dev-signing] created $CN in $KC"
security find-identity -p codesigning "$KC" | grep "$CN"
