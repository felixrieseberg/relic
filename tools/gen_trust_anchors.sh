#!/bin/sh
# Regenerate src/core/trust_anchors.h from pinned roots.
# Roots: GTS R4 (current chain), GTS R1 (insurance), GlobalSign R1 (cross-sign).
set -e
cd "$(dirname "$0")/.."
curl -fsSL https://pki.goog/repo/certs/gtsr4.pem -o tools/gtsr4.pem
curl -fsSL https://pki.goog/repo/certs/gtsr1.pem -o tools/gtsr1.pem
curl -fsSL https://secure.globalsign.net/cacert/Root-R1.crt \
  | openssl x509 -inform DER -out tools/globalsign-r1.pem
cat tools/gtsr4.pem tools/gtsr1.pem tools/globalsign-r1.pem > tools/roots.pem
{
  echo "#ifndef CORE_TRUST_ANCHORS_H"
  echo "#define CORE_TRUST_ANCHORS_H"
  echo
  ./third_party/bearssl/build/brssl ta tools/roots.pem 2>/dev/null \
    | grep -v "^Reading file"
  echo
  echo "#endif /* CORE_TRUST_ANCHORS_H */"
} > src/core/trust_anchors.h
rm -f tools/gtsr4.pem tools/gtsr1.pem tools/globalsign-r1.pem tools/roots.pem
echo "Wrote src/core/trust_anchors.h ($(grep -c BR_X509_TA_CA src/core/trust_anchors.h) anchors)"
