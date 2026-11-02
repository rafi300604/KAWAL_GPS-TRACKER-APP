#!/bin/sh
# Jalankan aplikasi dengan secrets.json (kunci API + akun Login Cepat) - jangan lupa flag ini lagi.
# Contoh: ./run.sh -d chrome    ./run.sh -d <id-hp>    ./run.sh -d chrome --profile
cd "$(dirname "$0")" && exec flutter run --dart-define-from-file=secrets.json "$@"
