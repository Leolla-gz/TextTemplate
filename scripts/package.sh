#!/bin/sh
# 配布用パッケージ(.au2pkg.zip)を作るスクリプト
#   使い方: sh scripts/package.sh 0.3.0
#   前提  : build_mingw.sh で build/TextTemplate.aux2 を作成済み
#   出力  : release/TextTemplate_v<版>.au2pkg.zip
#           (AviUtl2 のプレビュー画面へD&D、または AviUtl2 カタログ経由でインストールできる形式)
set -eu
cd "$(dirname "$0")/.."
VERSION="${1:?バージョンを指定してください 例: sh scripts/package.sh 0.3.0}"
PKG=release/pkg
rm -rf "$PKG"
mkdir -p "$PKG/Plugin/TextTemplate"

# プラグイン本体とライセンス表記(バイナリ配布時に同梱が必要)
cp build/TextTemplate.aux2 "$PKG/Plugin/TextTemplate/"
cp LICENSE "$PKG/Plugin/TextTemplate/LICENSE.txt"
cp THIRD_PARTY_NOTICES.txt "$PKG/Plugin/TextTemplate/"
# パッケージ情報(版数を埋め込む)
sed "s/@VERSION@/$VERSION/g" packaging/package.ini.in > "$PKG/package.ini"
sed "s/@VERSION@/$VERSION/g" packaging/package.txt.in > "$PKG/package.txt"

# zip 化(zip コマンドが無い環境でも動くよう python を使用)
OUT="release/TextTemplate_v$VERSION.au2pkg.zip"
rm -f "$OUT"
python3 - "$PKG" "$OUT" <<'PY'
import os, sys, zipfile
src, out = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for root, _, files in os.walk(src):
        for f in sorted(files):
            p = os.path.join(root, f)
            z.write(p, os.path.relpath(p, src))
PY
echo "packaged: $OUT"
