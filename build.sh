#!/usr/bin/env bash
# build.sh — Build PHP as a static library for a given target.
#
# This script is run by GitHub Actions CI, NOT by developers.
# Developers use `phptoro build` which downloads pre-built binaries.
#
# Usage: ./build.sh --php=8.5.3 --target=macos-arm64
#        ./build.sh --php=8.5.3 --target=android-arm64

set -eo pipefail

# ── Parse arguments ───────────────────────────────────────────────────────────

PHP_VERSION=""
TARGET=""
ICONV_VERSION="1.18"
LIBXML2_VERSION="2.13.6"
SQLITE_YEAR="2025"
SQLITE_VERSION="3490000"
OPENSSL_VERSION="3.5.5"
LIBSODIUM_VERSION="1.0.21"

for arg in "$@"; do
    case "$arg" in
        --php=*)        PHP_VERSION="${arg#*=}" ;;
        --target=*)     TARGET="${arg#*=}" ;;
        --iconv=*)      ICONV_VERSION="${arg#*=}" ;;
        --libxml2=*)    LIBXML2_VERSION="${arg#*=}" ;;
        --sqlite-year=*) SQLITE_YEAR="${arg#*=}" ;;
        --sqlite=*)     SQLITE_VERSION="${arg#*=}" ;;
        --openssl=*)    OPENSSL_VERSION="${arg#*=}" ;;
        --libsodium=*)  LIBSODIUM_VERSION="${arg#*=}" ;;
        --help|-h)
            echo "Usage: $0 --php=<version> --target=<target> [options]"
            echo ""
            echo "Required:"
            echo "  --php=VERSION        Full PHP version (e.g. 8.5.3)"
            echo "  --target=TARGET      macos-arm64, ios-arm64, ios-arm64-sim, android-arm64, android-x86_64"
            echo ""
            echo "Optional (override dependency versions):"
            echo "  --iconv=VERSION      libiconv (default: $ICONV_VERSION)"
            echo "  --libxml2=VERSION    libxml2 (default: $LIBXML2_VERSION)"
            echo "  --sqlite-year=YEAR   SQLite release year (default: $SQLITE_YEAR)"
            echo "  --sqlite=VERSION     SQLite version code (default: $SQLITE_VERSION)"
            echo "  --openssl=VERSION    OpenSSL (default: $OPENSSL_VERSION)"
            echo "  --libsodium=VERSION  libsodium (default: $LIBSODIUM_VERSION)"
            exit 0
            ;;
        *) echo "Unknown argument: $arg"; exit 1 ;;
    esac
done

if [[ -z "$PHP_VERSION" || -z "$TARGET" ]]; then
    echo "Error: --php and --target are required."
    echo "Run '$0 --help' for usage."
    exit 1
fi

# ── Validate PHP version (must be full x.y.z) ────────────────────────────────

if [[ ! "$PHP_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Error: --php requires a full version (e.g. 8.5.3), got '$PHP_VERSION'"
    exit 1
fi
PHP_FULL="$PHP_VERSION"

IOS_MIN="15.0"
ANDROID_API="24"

# ── Directories ──────────────────────────────────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/cache/$TARGET-php$PHP_FULL"
INSTALL_DIR="$BUILD_DIR/install"
DOWNLOAD_DIR="$SCRIPT_DIR/cache/downloads"
OUTPUT_DIR="$SCRIPT_DIR/output/php-$PHP_FULL-$TARGET"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"

mkdir -p "$BUILD_DIR" "$INSTALL_DIR/lib" "$INSTALL_DIR/include" "$DOWNLOAD_DIR" "$OUTPUT_DIR"

# ── Colors & helpers ─────────────────────────────────────────────────────────

RED='\033[0;31m'; GREEN='\033[0;32m'; CYAN='\033[0;36m'
BOLD='\033[1m'; DIM='\033[2m'; RESET='\033[0m'

step()  { echo -e "\n${BOLD}${CYAN}▸ $*${RESET}"; }
ok()    { echo -e "${GREEN}  ✔ $*${RESET}"; }
info()  { echo -e "${DIM}  $*${RESET}"; }
fail()  { echo -e "${RED}  ✘ $*${RESET}" >&2; }

trap 'fail "Failed at line $LINENO (exit $?): $BASH_COMMAND"' ERR

download() {
    local url="$1" dest="$2"
    local filename="$(basename "$dest")"
    local cached="$DOWNLOAD_DIR/$filename"
    if [[ -f "$cached" ]]; then
        info "cached: $filename"
    else
        info "downloading: $filename"
        curl -fSL --progress-bar "$url" -o "$cached"
    fi
    [[ -f "$dest" ]] || ln -sf "$cached" "$dest"
}

# ── Toolchain setup ─────────────────────────────────────────────────────────

CONFIGURE_HOST=""
IS_CROSS=false
IS_IOS=false
IS_ANDROID=false
MAKE_JOBS="$JOBS"

case "$TARGET" in
    macos-arm64)
        CFLAGS="-arch arm64 -O2"
        LDFLAGS="-arch arm64"
        CPPFLAGS=""
        ;;
    ios-arm64)
        IS_CROSS=true; IS_IOS=true; MAKE_JOBS=1
        SDK_PATH="$(xcrun --sdk iphoneos --show-sdk-path)"
        CC="$(xcrun --sdk iphoneos --find clang)"
        CXX="$(xcrun --sdk iphoneos --find clang++)"
        AR="$(xcrun --sdk iphoneos --find ar)"
        RANLIB="$(xcrun --sdk iphoneos --find ranlib)"
        STRIP="$(xcrun --sdk iphoneos --find strip)"
        CFLAGS="-arch arm64 -isysroot $SDK_PATH -miphoneos-version-min=$IOS_MIN -O2 -fPIC"
        LDFLAGS="-arch arm64 -isysroot $SDK_PATH -miphoneos-version-min=$IOS_MIN"
        CPPFLAGS="-isysroot $SDK_PATH"
        CONFIGURE_HOST="--host=aarch64-apple-darwin"
        ;;
    ios-arm64-sim)
        IS_CROSS=true; IS_IOS=true; MAKE_JOBS=1
        SDK_PATH="$(xcrun --sdk iphonesimulator --show-sdk-path)"
        CC="$(xcrun --sdk iphonesimulator --find clang)"
        CXX="$(xcrun --sdk iphonesimulator --find clang++)"
        AR="$(xcrun --sdk iphonesimulator --find ar)"
        RANLIB="$(xcrun --sdk iphonesimulator --find ranlib)"
        STRIP="$(xcrun --sdk iphonesimulator --find strip)"
        CFLAGS="-arch arm64 -isysroot $SDK_PATH -mios-simulator-version-min=$IOS_MIN -O2 -fPIC"
        LDFLAGS="-arch arm64 -isysroot $SDK_PATH -mios-simulator-version-min=$IOS_MIN"
        CPPFLAGS="-isysroot $SDK_PATH"
        CONFIGURE_HOST="--host=aarch64-apple-darwin"
        ;;
    android-arm64)
        IS_CROSS=true; IS_ANDROID=true; MAKE_JOBS=1
        ANDROID_TRIPLE="aarch64-linux-android"
        ;;
    android-x86_64)
        IS_CROSS=true; IS_ANDROID=true; MAKE_JOBS=1
        ANDROID_TRIPLE="x86_64-linux-android"
        ;;
    *)
        echo "Unknown target: $TARGET"
        exit 1
        ;;
esac

# Android NDK setup
if [[ "$IS_ANDROID" == true ]]; then
    if [[ -z "${ANDROID_NDK_ROOT:-}" && -n "${ANDROID_HOME:-}" ]]; then
        ANDROID_NDK_ROOT="$(ls -d "$ANDROID_HOME/ndk/"* 2>/dev/null | sort -V | tail -1)"
    fi
    [[ -z "${ANDROID_NDK_ROOT:-}" ]] && { fail "ANDROID_NDK_ROOT not set"; exit 1; }

    NDK_HOST="$(uname -s | sed 's/Darwin/darwin-x86_64/;s/Linux/linux-x86_64/')"
    TOOLCHAIN="$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/$NDK_HOST"
    export PATH="$TOOLCHAIN/bin:$PATH"
    CC="$TOOLCHAIN/bin/${ANDROID_TRIPLE}${ANDROID_API}-clang"
    CXX="$TOOLCHAIN/bin/${ANDROID_TRIPLE}${ANDROID_API}-clang++"
    AR="$TOOLCHAIN/bin/llvm-ar"
    RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
    STRIP="$TOOLCHAIN/bin/llvm-strip"
    CFLAGS="-O2 -fPIC"
    LDFLAGS=""
    CPPFLAGS=""
    CONFIGURE_HOST="--host=$ANDROID_TRIPLE"
fi

# ── Banner ───────────────────────────────────────────────────────────────────

echo -e "${BOLD}${CYAN}"
echo "  ╔══════════════════════════════════════════╗"
echo "  ║  phpToro Runtime Builder                 ║"
echo "  ║  PHP $PHP_FULL — $TARGET"
echo "  ╚══════════════════════════════════════════╝"
echo -e "${RESET}"
echo "  Output: $OUTPUT_DIR"
echo "  Jobs:   $JOBS"

# ── Build dependencies ───────────────────────────────────────────────────────

# libiconv
step "libiconv $ICONV_VERSION"
if [[ -f "$INSTALL_DIR/lib/libiconv.a" ]]; then ok "skipping"; else
    download "https://ftp.gnu.org/pub/gnu/libiconv/libiconv-$ICONV_VERSION.tar.gz" "$BUILD_DIR/libiconv-$ICONV_VERSION.tar.gz"
    rm -rf "$BUILD_DIR/libiconv-$ICONV_VERSION"; tar xzf "$BUILD_DIR/libiconv-$ICONV_VERSION.tar.gz" -C "$BUILD_DIR"
    cd "$BUILD_DIR/libiconv-$ICONV_VERSION"
    info "configuring..."; ./configure $CONFIGURE_HOST --prefix="$INSTALL_DIR" --enable-static --disable-shared CC="${CC:-cc}" CFLAGS="$CFLAGS" LDFLAGS="$LDFLAGS" CPPFLAGS="$CPPFLAGS" >"$BUILD_DIR/iconv.log" 2>&1
    info "compiling..."; make -j"$JOBS" >>"$BUILD_DIR/iconv.log" 2>&1
    info "installing..."; make install >>"$BUILD_DIR/iconv.log" 2>&1
    ok "libiconv"
fi

# libxml2
step "libxml2 $LIBXML2_VERSION"
if [[ -f "$INSTALL_DIR/lib/libxml2.a" ]]; then ok "skipping"; else
    download "https://download.gnome.org/sources/libxml2/${LIBXML2_VERSION%.*}/libxml2-$LIBXML2_VERSION.tar.xz" "$BUILD_DIR/libxml2-$LIBXML2_VERSION.tar.xz"
    rm -rf "$BUILD_DIR/libxml2-$LIBXML2_VERSION"; tar xf "$BUILD_DIR/libxml2-$LIBXML2_VERSION.tar.xz" -C "$BUILD_DIR"
    cd "$BUILD_DIR/libxml2-$LIBXML2_VERSION"
    info "configuring..."; ./configure $CONFIGURE_HOST --prefix="$INSTALL_DIR" --enable-static --disable-shared --without-python --without-lzma --without-zlib --without-http --without-ftp --with-iconv="$INSTALL_DIR" CC="${CC:-cc}" CFLAGS="$CFLAGS -I$INSTALL_DIR/include" LDFLAGS="$LDFLAGS" CPPFLAGS="$CPPFLAGS -I$INSTALL_DIR/include" >"$BUILD_DIR/xml2.log" 2>&1
    info "compiling..."; make -j"$JOBS" >>"$BUILD_DIR/xml2.log" 2>&1
    info "installing..."; make install >>"$BUILD_DIR/xml2.log" 2>&1
    ok "libxml2"
fi

# SQLite
step "SQLite $SQLITE_VERSION"
if [[ -f "$INSTALL_DIR/lib/libsqlite3.a" ]]; then ok "skipping"; else
    download "https://www.sqlite.org/$SQLITE_YEAR/sqlite-autoconf-$SQLITE_VERSION.tar.gz" "$BUILD_DIR/sqlite-autoconf-$SQLITE_VERSION.tar.gz"
    rm -rf "$BUILD_DIR/sqlite-autoconf-$SQLITE_VERSION"; tar xzf "$BUILD_DIR/sqlite-autoconf-$SQLITE_VERSION.tar.gz" -C "$BUILD_DIR"
    cd "$BUILD_DIR/sqlite-autoconf-$SQLITE_VERSION"
    info "configuring..."; ./configure $CONFIGURE_HOST --prefix="$INSTALL_DIR" --enable-static --disable-shared CC="${CC:-cc}" CFLAGS="$CFLAGS -DSQLITE_THREADSAFE=0" LDFLAGS="$LDFLAGS" CPPFLAGS="$CPPFLAGS" >"$BUILD_DIR/sqlite.log" 2>&1
    info "compiling..."; make -j"$JOBS" sqlite3.o >>"$BUILD_DIR/sqlite.log" 2>&1; make -j"$JOBS" install-lib install-headers >>"$BUILD_DIR/sqlite.log" 2>&1
    ok "SQLite"
fi

# OpenSSL
step "OpenSSL $OPENSSL_VERSION"
if [[ -f "$INSTALL_DIR/lib/libssl.a" ]]; then ok "skipping"; else
    download "https://github.com/openssl/openssl/releases/download/openssl-$OPENSSL_VERSION/openssl-$OPENSSL_VERSION.tar.gz" "$BUILD_DIR/openssl-$OPENSSL_VERSION.tar.gz"
    rm -rf "$BUILD_DIR/openssl-$OPENSSL_VERSION"; tar xzf "$BUILD_DIR/openssl-$OPENSSL_VERSION.tar.gz" -C "$BUILD_DIR"
    cd "$BUILD_DIR/openssl-$OPENSSL_VERSION"
    case "$TARGET" in
        macos-arm64)      SSL_TARGET="darwin64-arm64-cc"; SSL_EXTRA="" ;;
        ios-arm64)        SSL_TARGET="ios64-xcrun"; SSL_EXTRA="-miphoneos-version-min=$IOS_MIN" ;;
        ios-arm64-sim)    SSL_TARGET="iossimulator-xcrun"; SSL_EXTRA="-mios-simulator-version-min=$IOS_MIN" ;;
        android-arm64)    SSL_TARGET="android-arm64"; SSL_EXTRA="-D__ANDROID_API__=$ANDROID_API" ;;
        android-x86_64)   SSL_TARGET="android-x86_64"; SSL_EXTRA="-D__ANDROID_API__=$ANDROID_API" ;;
    esac
    NO_EXTRAS="no-shared no-tests"
    [[ "$IS_ANDROID" == true || "$IS_IOS" == true ]] && NO_EXTRAS="no-shared no-tests no-ui-console no-async no-engine no-dso"
    info "configuring ($SSL_TARGET)..."
    if [[ "$IS_ANDROID" == true ]]; then
        ANDROID_NDK_ROOT="$ANDROID_NDK_ROOT" ./Configure "$SSL_TARGET" --prefix="$INSTALL_DIR" $NO_EXTRAS $SSL_EXTRA >"$BUILD_DIR/openssl.log" 2>&1
    else
        ./Configure "$SSL_TARGET" --prefix="$INSTALL_DIR" $NO_EXTRAS $SSL_EXTRA >"$BUILD_DIR/openssl.log" 2>&1
    fi
    info "compiling..."; make -j"$JOBS" >>"$BUILD_DIR/openssl.log" 2>&1
    info "installing..."; make install_sw >>"$BUILD_DIR/openssl.log" 2>&1
    ok "OpenSSL"
fi

# libsodium
step "libsodium $LIBSODIUM_VERSION"
if [[ -f "$INSTALL_DIR/lib/libsodium.a" ]]; then ok "skipping"; else
    download "https://download.libsodium.org/libsodium/releases/libsodium-$LIBSODIUM_VERSION-stable.tar.gz" "$BUILD_DIR/libsodium-$LIBSODIUM_VERSION-stable.tar.gz"
    rm -rf "$BUILD_DIR/libsodium-stable"; tar xzf "$BUILD_DIR/libsodium-$LIBSODIUM_VERSION-stable.tar.gz" -C "$BUILD_DIR"
    cd "$BUILD_DIR/libsodium-stable"
    SODIUM_EXTRA="--disable-pie"
    [[ "$IS_ANDROID" == true ]] && SODIUM_EXTRA="$SODIUM_EXTRA --disable-ssp"
    info "configuring..."; ./configure $CONFIGURE_HOST --prefix="$INSTALL_DIR" --enable-static --disable-shared $SODIUM_EXTRA CC="${CC:-cc}" CFLAGS="$CFLAGS" LDFLAGS="$LDFLAGS" CPPFLAGS="$CPPFLAGS" >"$BUILD_DIR/sodium.log" 2>&1
    info "compiling..."; make -j"$JOBS" >>"$BUILD_DIR/sodium.log" 2>&1
    info "installing..."; make install >>"$BUILD_DIR/sodium.log" 2>&1
    ok "libsodium"
fi

# ── Build PHP ────────────────────────────────────────────────────────────────

step "PHP $PHP_FULL"
if [[ -f "$INSTALL_DIR/lib/libphp.a" ]]; then ok "skipping"; else
    download "https://www.php.net/distributions/php-$PHP_FULL.tar.xz" "$BUILD_DIR/php-$PHP_FULL.tar.xz"
    rm -rf "$BUILD_DIR/php-$PHP_FULL"; tar xf "$BUILD_DIR/php-$PHP_FULL.tar.xz" -C "$BUILD_DIR"
    cd "$BUILD_DIR/php-$PHP_FULL"

    PHP_ARGS=(
        $CONFIGURE_HOST --prefix="$INSTALL_DIR"
        --enable-embed=static --disable-cli --disable-phpdbg --disable-cgi --disable-phar
        --without-pear --without-valgrind --disable-all
        --enable-json --enable-filter --enable-ctype --enable-session
        --enable-mbstring --disable-mbregex --enable-pdo --enable-spl
        --enable-bcmath --enable-tokenizer --disable-opcache-jit
        --enable-dom --enable-simplexml --enable-xml
        --with-iconv="$INSTALL_DIR" --with-libxml="$INSTALL_DIR"
        --with-pdo-sqlite="$INSTALL_DIR" --with-sqlite3="$INSTALL_DIR"
        --with-openssl="$INSTALL_DIR" --with-sodium="$INSTALL_DIR"
    )

    [[ "$IS_CROSS" == true ]] && PHP_ARGS+=(--disable-zts --without-pcre-jit)
    [[ "$IS_IOS" == true ]] && PHP_ARGS+=(--disable-fiber-asm ac_cv_func_posix_spawn_file_actions_addchdir_np=no ac_cv_func_procctl=no ac_cv_func_dns_search=no ac_cv_func_res_search=no ac_cv_header_dns_h=no)
    [[ "$IS_ANDROID" == true ]] && PHP_ARGS+=(ac_cv_func_posix_spawn_file_actions_addchdir_np=no ac_cv_func_procctl=no ac_cv_header_ucontext_h=no ac_cv_func_makecontext=no ac_cv_func_getpwnam_r=no ac_cv_func_getpwuid_r=no ac_cv_func_dlopen=yes ac_cv_lib_dl_dlopen=no ac_cv_func_getdtablesize=no ac_cv_func_dns_search=no ac_cv_func_res_search=no ac_cv_func_res_nsearch=no ac_cv_func_res_ninit=no ac_cv_func_res_nclose=no ac_cv_func_dn_skipname=no ac_cv_header_dns_h=no ac_cv_header_resolv_h=no)

    PHP_ARGS+=(
        CFLAGS="$CFLAGS -I$INSTALL_DIR/include -I$INSTALL_DIR/include/libxml2"
        LDFLAGS="$LDFLAGS -L$INSTALL_DIR/lib"
        CPPFLAGS="$CPPFLAGS -I$INSTALL_DIR/include -I$INSTALL_DIR/include/libxml2"
    )
    [[ "$IS_CROSS" == true ]] && PHP_ARGS+=(CC="$CC" CXX="$CXX" AR="$AR" RANLIB="$RANLIB" STRIP="$STRIP" CXXFLAGS="$CFLAGS")

    info "configuring PHP..."
    ./configure "${PHP_ARGS[@]}" >"$BUILD_DIR/php-configure.log" 2>&1

    # Patch unavailable APIs
    if [[ "$IS_CROSS" == true ]]; then
        sed -i '' -e 's/^#define HAVE_DNS_SEARCH 1/\/* #undef HAVE_DNS_SEARCH *\//' -e 's/^#define HAVE_RES_SEARCH 1/\/* #undef HAVE_RES_SEARCH *\//' -e 's/^#define HAVE_FULL_WRITE_BUFFER 1/\/* #undef HAVE_FULL_WRITE_BUFFER *\//' main/php_config.h
    fi
    if [[ "$IS_ANDROID" == true ]]; then
        sed -i '' 's/dtablesize = getdtablesize();/dtablesize = (int)sysconf(_SC_OPEN_MAX);/' ext/standard/php_fopen_wrapper.c
        sed -i '' -e 's/^#define HAVE_RES_NSEARCH 1/\/* #undef HAVE_RES_NSEARCH *\//' -e 's/^#define HAVE_RES_NINIT 1/\/* #undef HAVE_RES_NINIT *\//' -e 's/^#define HAVE_RES_NCLOSE 1/\/* #undef HAVE_RES_NCLOSE *\//' -e 's/^#define HAVE_DN_SKIPNAME 1/\/* #undef HAVE_DN_SKIPNAME *\//' -e 's/^#define HAVE_RESOLV_H 1/\/* #undef HAVE_RESOLV_H *\//' -e 's/^#define HAVE_GETDTABLESIZE 1/\/* #undef HAVE_GETDTABLESIZE *\//' main/php_config.h
    fi

    info "compiling PHP (jobs=$MAKE_JOBS)..."
    make -j"$MAKE_JOBS" >"$BUILD_DIR/php-make.log" 2>&1
    make install-sapi install-headers >>"$BUILD_DIR/php-make.log" 2>&1
    ok "PHP $PHP_FULL"
fi

# ── Build phpToro extension ──────────────────────────────────────────────────

step "phpToro runtime (sapi + ext + phpinfo)"
if [[ -f "$INSTALL_DIR/lib/libphptoro_ext.a" ]]; then ok "skipping"; else
    PHP_INCLUDES="-I$INSTALL_DIR/include/php -I$INSTALL_DIR/include/php/main -I$INSTALL_DIR/include/php/TSRM -I$INSTALL_DIR/include/php/Zend -I$INSTALL_DIR/include/php/ext/json -I$SCRIPT_DIR/ext"
    info "compiling phptoro_sapi.c..."
    ${CC:-cc} $CFLAGS $PHP_INCLUDES -c "$SCRIPT_DIR/ext/phptoro_sapi.c" -o "$BUILD_DIR/phptoro_sapi.o"
    info "compiling phptoro_ext.c..."
    ${CC:-cc} $CFLAGS $PHP_INCLUDES -c "$SCRIPT_DIR/ext/phptoro_ext.c" -o "$BUILD_DIR/phptoro_ext.o"
    info "compiling phptoro_phpinfo.c..."
    ${CC:-cc} $CFLAGS $PHP_INCLUDES -c "$SCRIPT_DIR/ext/phptoro_phpinfo.c" -o "$BUILD_DIR/phptoro_phpinfo.o"
    info "creating libphptoro_ext.a..."
    ${AR:-ar} rcs "$INSTALL_DIR/lib/libphptoro_ext.a" \
        "$BUILD_DIR/phptoro_sapi.o" \
        "$BUILD_DIR/phptoro_ext.o" \
        "$BUILD_DIR/phptoro_phpinfo.o"
    ok "libphptoro_ext.a"
fi

# ── Package output ───────────────────────────────────────────────────────────

step "Packaging → $OUTPUT_DIR"
rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR/lib" "$OUTPUT_DIR/include"

for lib in libphp libphptoro_ext libiconv libcharset libxml2 libsqlite3 libssl libcrypto libsodium; do
    [[ -f "$INSTALL_DIR/lib/${lib}.a" ]] && cp "$INSTALL_DIR/lib/${lib}.a" "$OUTPUT_DIR/lib/"
done

# PHP headers
cp -R "$INSTALL_DIR/include/php" "$OUTPUT_DIR/include/"

# phpToro headers
cp "$SCRIPT_DIR/ext/phptoro_sapi.h" "$OUTPUT_DIR/include/"
cp "$SCRIPT_DIR/ext/phptoro_ext.h" "$OUTPUT_DIR/include/"
cp "$SCRIPT_DIR/ext/phptoro_phpinfo.h" "$OUTPUT_DIR/include/"

# Create archive for GitHub Releases
ARCHIVE="$SCRIPT_DIR/output/php-$PHP_FULL-$TARGET.tar.gz"
tar czf "$ARCHIVE" -C "$SCRIPT_DIR/output" "php-$PHP_FULL-$TARGET"

echo ""
echo -e "${BOLD}${GREEN}  ✔ php-$PHP_FULL-$TARGET built successfully!${RESET}"
echo ""
for f in "$OUTPUT_DIR/lib/"*.a; do
    echo "    $(basename "$f")  ($(du -sh "$f" | cut -f1))"
done
echo ""
echo "  Archive: $ARCHIVE"
echo ""
