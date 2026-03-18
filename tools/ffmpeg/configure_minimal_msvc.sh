#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 <ffmpeg-src-dir> <install-prefix>" >&2
  exit 1
fi

src_dir="$1"
install_prefix="$2"

if [[ ! -d "$src_dir" ]]; then
  echo "ffmpeg source dir not found: $src_dir" >&2
  exit 1
fi

if [[ -z "${INCLUDE:-}" || -z "${LIB:-}" ]]; then
  echo "MSVC environment is not initialized (INCLUDE/LIB missing)." >&2
  exit 1
fi

msvc_cl="$(command -v cl || true)"
if [[ -z "$msvc_cl" ]]; then
  echo "cl not found in PATH." >&2
  exit 1
fi

msvc_bin_dir="$(dirname "$msvc_cl")"
msvc_link="$msvc_bin_dir/link.exe"
msvc_lib="$msvc_bin_dir/lib.exe"

if [[ ! -x "$msvc_link" || ! -x "$msvc_lib" ]]; then
  echo "MSVC linker tools not found next to cl: $msvc_bin_dir" >&2
  exit 1
fi

cd "$src_dir"
make distclean >/dev/null 2>&1 || true

mkdir -p ffbuild
msvc_link_wrapper="$src_dir/ffbuild/msvc-link-wrapper.sh"
msvc_lib_wrapper="$src_dir/ffbuild/msvc-lib-wrapper.sh"

cat > "$msvc_link_wrapper" <<EOF
#!/usr/bin/env bash
exec "$msvc_link" "\$@"
EOF
cat > "$msvc_lib_wrapper" <<EOF
#!/usr/bin/env bash
exec "$msvc_lib" "\$@"
EOF
chmod +x "$msvc_link_wrapper" "$msvc_lib_wrapper"

./configure \
  --prefix="$install_prefix" \
  --toolchain=msvc \
  --arch=x86_64 \
  --target-os=win64 \
  --enable-shared \
  --disable-static \
  --disable-avdevice \
  --disable-avfilter \
  --disable-swresample \
  --disable-programs \
  --disable-doc \
  --disable-network \
  --disable-autodetect \
  --disable-debug \
  --disable-everything \
  --enable-avcodec \
  --enable-avformat \
  --enable-avutil \
  --enable-swscale \
  --enable-protocol=file \
  --enable-demuxer=mov \
  --enable-muxer=mp4 \
  --enable-decoder=h264 \
  --enable-parser=h264 \
  --enable-encoder=h264_mf \
  --enable-bsf=aac_adtstoasc \
  --enable-mediafoundation \
  --ld="$msvc_link_wrapper" \
  --ar="$msvc_lib_wrapper"

# Some localized MSVC banners contain non-ASCII text, which can break rc.exe
# when parsing config.h as ANSI. Normalize CC_IDENT to a safe ASCII string.
sed -i 's|^#define CC_IDENT .*|#define CC_IDENT "MSVC"|g' config.h

# FFmpeg 7.1.x may omit aom_film_grain.o from H264 SEI object list under
# --disable-everything minimal configs, causing unresolved symbols at link time.
sed -i 's|^OBJS-\$(CONFIG_H264_SEI)[[:space:]]*+= h264_sei.o h2645_sei.o$|OBJS-$(CONFIG_H264_SEI)                += h264_sei.o h2645_sei.o aom_film_grain.o|g' libavcodec/Makefile
