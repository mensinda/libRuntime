#!/usr/bin/bash

DD="$(command -v dd)"
OBJCOPY="$(command -v objcopy)"

EMBED=0

help_msg() {
    cat <<EOF
    Patch the generate runtime ELF executalbe to be compatible with AppImage

    Usage: $0 -i <IN> -o <OUT> -p <DIR> [extra options]

    Options:
        -h | --help                -- Print this help message and exit
        -i | --input        <IN>   -- Path to the raw executable                    [REQUIRED]
        -o | --output       <IN>   -- Path to the patched output executable         [REQUIRED]
        -p | --private-dir  <DIR>  -- A private directory for temporary files       [REQUIRED]
        -e | --embed-bytes         -- Set to enable embedding magic AppImage bytes
        --dd                <EXE>  -- Path to the dd executalbe                     [DEFAULT=$DD]
        --objcopy           <EXE>  -- Path to the objcopy executalbe                [DEFAULT=$OBJCOPY]
EOF
}

while [[ $# -gt 0 ]]; do
  key="$1"

  case "$key" in
    -i|--input)
        INPUT="$2"
        shift; shift ;;
    -o|--output)
        OUTPUT="$2"
        shift; shift ;;
    -p|--private-dir)
        PRIVATE_DIR="$2"
        shift; shift ;;
    -e|--embed-bytes)
        EMBED=1
        shift ;;
    --dd)
        DD="$2"
        shift; shift ;;
    --objcopy)
        OBJCOPY="$2"
        shift; shift ;;
    -h|--help)
        help_msg
        exit 0
        ;;
    *) # unknown option
        echo "unknown option '$1'"
        shift ;;
  esac
done

missing=0
for i in INPUT OUTPUT PRIVATE_DIR DD OBJCOPY; do
    if [[ "${!i}" != '' ]]; then
        continue
    fi
    echo "MISSING VARIABLE: $i"
    (( missing++ ))
done

if (( missing > 0 )); then
    help_msg
    exit 1
fi

[ -e "$PRIVATE_DIR" ] && rm -rf "$PRIVATE_DIR"
[ -e "$OUTPUT" ]      && rm -rf "$OUTPUT"
mkdir -p "$PRIVATE_DIR"

for i in 16 1024 8192; do
    $DD if=/dev/zero bs=1 count=$i of="$PRIVATE_DIR/${i}_zeros" &> /dev/null
done

cp "$INPUT" "$PRIVATE_DIR/temp0"

$OBJCOPY --add-section  .digest_md5="$PRIVATE_DIR/16_zeros"    --set-section-flags  .digest_md5=noload,readonly  "$PRIVATE_DIR/temp0" "$PRIVATE_DIR/temp1"
$OBJCOPY --add-section    .upd_info="$PRIVATE_DIR/1024_zeros"  --set-section-flags    .upd_info=noload,readonly  "$PRIVATE_DIR/temp1" "$PRIVATE_DIR/temp2"
$OBJCOPY --add-section  .sha256_sig="$PRIVATE_DIR/1024_zeros"  --set-section-flags  .sha256_sig=noload,readonly  "$PRIVATE_DIR/temp2" "$PRIVATE_DIR/temp3"
$OBJCOPY --add-section     .sig_key="$PRIVATE_DIR/8192_zeros"  --set-section-flags     .sig_key=noload,readonly  "$PRIVATE_DIR/temp3" "$PRIVATE_DIR/temp4"

if (( EMBED > 0 )); then
    echo -ne 'AI\x02' | dd of="$PRIVATE_DIR/temp4" bs=1 count=3 seek=8 conv=notrunc
fi

cp "$PRIVATE_DIR/temp4" "$OUTPUT"
