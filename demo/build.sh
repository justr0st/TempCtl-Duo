#!/bin/bash
# Browser demo (demo/README.md): the firmware's control code + demo/sim.cpp -> demo/out/tempctl-demo.wasm
#   WASI_SDK=<wasi-sdk 24+ folder> demo/build.sh       with the wasi-sdk (clang included)
#   WASI_SYSROOT=<wasi-sysroot> WASI_RT=<libclang_rt.builtins-wasm32.a> demo/build.sh
#                                                       with the system's clang + wasm-ld (lld)
#   AJ=<ArduinoJson src folder> (default: .pio/libdeps/s3/ArduinoJson/src)
set -e
cd "$(dirname "$0")/.."
OUT=demo/out
OBJ=$OUT/obj
mkdir -p $OBJ
AJ=${AJ:-.pio/libdeps/s3/ArduinoJson/src}
[ -f "$AJ/ArduinoJson.h" ] || { echo "ArduinoJson not found (AJ=$AJ)"; exit 1; }
if [ -n "$WASI_SDK" ]; then
  CXX="$WASI_SDK/bin/clang++"
  TGT="--target=wasm32-wasi --sysroot=$WASI_SDK/share/wasi-sysroot"
else
  CXX=${CXX:-clang++}
  [ -d "$WASI_SYSROOT" ] && [ -f "$WASI_RT" ] || { echo "set WASI_SDK, or WASI_SYSROOT and WASI_RT"; exit 1; }
  # clang looks for the builtins in its resource folder: a copy with them added
  RD=$OBJ/clang-rt
  if [ ! -f $RD/lib/wasi/libclang_rt.builtins-wasm32.a ]; then
    rm -rf $RD
    mkdir -p $RD/lib/wasi
    ln -s "$($CXX -print-resource-dir)/include" $RD/include
    cp "$WASI_RT" $RD/lib/wasi/libclang_rt.builtins-wasm32.a
  fi
  TGT="--target=wasm32-wasi --sysroot=$WASI_SYSROOT -resource-dir=$RD"
fi
SRCS="app_state web local_ctrl settings settings_store run_engine duo_program program_store channel controller pid autotune
      runaway safety tc_sensor crumbs syslog modbus_rtu"
DEF="-DBOARD_DEMO -DBOARD_HAS_PSRAM -DARDUINO=10819 -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 -DARDUINOJSON_ENABLE_ARDUINO_STREAM=1
     -DARDUINOJSON_ENABLE_ARDUINO_PRINT=1 -DARDUINOJSON_ENABLE_PROGMEM=0"
FL="$TGT -O2 -std=gnu++17 -fno-exceptions -Wall -Wno-unused-function -Wno-unused-variable -Wno-bitwise-instead-of-logical -include demo/shim/demo_pre.h
    -Idemo/shim -Idemo -Isrc -I$AJ $DEF"
objs=()
pids=()
for f in $SRCS; do
  o=$OBJ/$f.o
  objs+=($o)
  $CXX $FL -c src/$f.cpp -o $o &
  pids+=($!)
done
for f in sim stubs; do
  o=$OBJ/demo_$f.o
  objs+=($o)
  $CXX $FL -c demo/$f.cpp -o $o &
  pids+=($!)
done
for p in "${pids[@]}"; do wait $p; done
# a reactor: no main(), the page calls _initialize and then the exports (sim.cpp)
$CXX $TGT -O2 -mexec-model=reactor -Wl,--strip-debug -Wl,-z,stack-size=262144 -Wl,--initial-memory=16777216 \
  -Wl,--max-memory=134217728 "${objs[@]}" -o $OUT/tempctl-demo.wasm
ls -l $OUT/tempctl-demo.wasm
