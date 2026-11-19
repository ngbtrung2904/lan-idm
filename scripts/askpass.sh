#!/bin/sh
# Minimal SSH_ASKPASS helper for Data Collector.
# Reads the passphrase from a single child-process environment variable
# set exclusively for the ssh-add invocation and prints it to stdout.
case "${1:-}" in
    *"Bad passphrase"*|*"try again"*)
        exit 1
        ;;
esac
printf '%s\n' "${DC_SSH_PASSPHRASE:-}"

