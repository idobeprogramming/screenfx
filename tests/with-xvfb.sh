#!/usr/bin/env sh
set -eu
if [ "${1:-}" != "--inside" ]; then
    # WSLg owns /tmp/.X11-unix. Keep Xvfb's generated authentication cookie,
    # and use its TCP transport so the fixture does not alter that directory.
    exec xvfb-run -a -l -e /dev/stderr -s '-screen 0 1280x1024x24 -nolisten unix -listen tcp' sh "$0" --inside "$@"
fi
shift
export DISPLAY="127.0.0.1${DISPLAY}"
exec "$@"
