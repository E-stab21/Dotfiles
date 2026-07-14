source /usr/share/cachyos-fish-config/cachyos-config.fish

# pyenv: enable only for interactive shells so non-interactive tooling
# (e.g. package builds) uses system python (/usr/bin/python3).
# if status --is-interactive
#     set -gx PYENV_ROOT $HOME/.pyenv
#     fish_add_path $PYENV_ROOT/bin
# end
# 
# # Login-shell PATH setup (like `pyenv init --path`)
# if status --is-login
#     pyenv init --path fish | source
# end
# 
# # Interactive shell integration (like `pyenv init -`)
# if status --is-interactive
#     pyenv init - fish | source
# end

# overwrite greeting
# potentially disabling fastfetch
# function fish_greeting
#    # smth smth
# end
