#!/usr/bin/env bash
# Download Qt for HarmonyOS (arm64) and the matching macOS host Qt from the
# public Qt online repository into $QT_ROOT/{harmonyos,macos}, the layout the
# Qt online installer would produce. Idempotent: modules already unpacked
# are skipped. QtPdf is not among Qt's HarmonyOS packages — build-qtpdf.sh
# builds it from source afterwards.
set -euo pipefail
source "$(dirname "$0")/env.sh"
command -v 7zz >/dev/null || die "7zz not found (brew install sevenzip)"

ver_nodot="${QT_VERSION//./}"
repo=https://download.qt.io/online/qtsdkrepository
ohos_base="$repo/all_os/harmonyos/qt6_$ver_nodot/qt6_${ver_nodot}_arm64_v8a"
host_base="$repo/mac_x64/desktop/qt6_$ver_nodot/qt6_$ver_nodot"
dl="$QT_ROOT/.downloads"
mkdir -p "$dl" "$QT_OHOS" "$QT_HOST"

# Prints "<version-prefix> <archive> ..." for one package of an Updates.xml.
archives() {  # <updates.xml> <package name>
    python3 - "$1" "$2" <<'EOF'
import re, sys
xml, name = open(sys.argv[1]).read(), sys.argv[2]
for m in re.finditer(r"<PackageUpdate>(.*?)</PackageUpdate>", xml, re.S):
    b = m.group(1)
    if re.search(r"<Name>(.*?)</Name>", b).group(1) != name:
        continue
    ver = re.search(r"<Version>(.*?)</Version>", b).group(1)
    arch = re.search(r"<DownloadableArchives>(.*?)</DownloadableArchives>", b, re.S)
    print(ver, *[a.strip() for a in (arch.group(1).split(",") if arch else [])])
EOF
}

fetch() {  # <base url> <package> <dest prefix> <module filter regex>
    local base=$1 pkg=$2 dest=$3 want=$4 xml line ver a
    xml="$dl/$(echo "$base" | md5 -q).xml"
    [ -s "$xml" ] || curl -sfL --retry 3 -o "$xml" "$base/Updates.xml"
    line=$(archives "$xml" "$pkg")
    [ -n "$line" ] || die "package $pkg not found in $base"
    ver=${line%% *}
    for a in ${line#* }; do
        [[ "$a" =~ ^($want)- ]] || continue
        local stamp="$dest/.unpacked-$a"
        [ -e "$stamp" ] && continue
        echo "  $pkg: $a"
        curl -sfL --retry 5 -C - -o "$dl/$a" "$base/$pkg/$ver$a"
        7zz t "$dl/$a" >/dev/null || die "corrupt download $a (re-run to resume)"
        7zz x -y -o"$dest" "$dl/$a" >/dev/null
        touch "$stamp"
    done
}

echo "Qt $QT_VERSION for HarmonyOS -> $QT_OHOS"
fetch "$ohos_base" "qt.qt6.$ver_nodot.harmonyos_arm64_v8a" "$QT_OHOS" \
      "qtbase|qtdeclarative|qtsvg|qttools|qttranslations"
fetch "$ohos_base" "qt.qt6.$ver_nodot.addons.qtwebsockets.harmonyos_arm64_v8a" "$QT_OHOS" \
      "qtwebsockets"

echo "Qt $QT_VERSION for macOS (host tools) -> $QT_HOST"
fetch "$host_base" "qt.qt6.$ver_nodot.clang_64" "$QT_HOST" \
      "qtbase|qtdeclarative|qtsvg|qttools"

# The HarmonyOS packages are built on Linux; point their host references at
# our macOS host Qt (what the installer patches in on install).
conf="$QT_OHOS/bin/target_qt.conf"
sed -i '' -e 's#^HostPrefix=.*#HostPrefix=../../macos#' \
          -e 's#^HostData=.*#HostData=../harmonyos#' \
          -e 's#^HostSpec=.*#HostSpec=macx-clang#' "$conf"

echo "done. Next: tools/ohos/build-qtpdf.sh"
