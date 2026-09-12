function plasma --description "Start a Plasma Wayland session on the current TTY"
    if test -n "$WAYLAND_DISPLAY"; or test -n "$DISPLAY"
        echo "plasma: a graphical session is already running here." >&2
        return 1
    end

    if not command -q startplasma-wayland
        echo "plasma: startplasma-wayland not found." >&2
        return 127
    end

    set -l log $HOME/.local/state/plasma-session.log
    mkdir -p (path dirname $log)

    startplasma-wayland >$log 2>&1
    set -l code $status

    if test $code -ne 0
        echo "plasma: session exited with status $code, see $log" >&2
    end

    return $code
end
