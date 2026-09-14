# GitHub SSH proxy path

The RISC-V development server has an unreliable direct TCP path to
`ssh.github.com:443`. Repeated probes showed alternating connection timeouts
and immediate success before SSH authentication. The GitHub key and repository
permissions were not involved.

The proxy listens on Windows loopback at `127.0.0.1:7897`; it is deliberately
not exposed to the LAN. Every Windows SSH connection that will run a GitHub
operation on the server must explicitly create this reverse forward:

```text
Windows proxy 127.0.0.1:7897
        |
        | ssh -R
        v
RISC-V server 127.0.0.1:17897
        |
        | HTTP CONNECT from nc
        v
ssh.github.com:443
```

Use the Windows OpenSSH client from WSL as follows:

```sh
/mnt/c/Windows/System32/OpenSSH/ssh.exe \
    -o ExitOnForwardFailure=yes \
    -R 127.0.0.1:17897:127.0.0.1:7897 \
    -p 2333 tc@10.156.112.63
```

The reverse forward lasts only for that SSH connection. Ordinary connections
that do not fetch or push need no forwarding option. The server's
`~/.ssh/config` selects the local forwarded proxy for `ssh.github.com`, so
repository remotes remain ordinary SSH URLs and Git commands need no
per-command environment variables:

```text
ssh://git@ssh.github.com:443/sasasashui/vart.git
```

## Verification

On the server, verify the listening reverse forward and GitHub authentication:

```sh
ss -lnt | grep 17897
ssh -T git@ssh.github.com
```

GitHub returns exit status 1 after printing its successful-authentication
message because it does not provide an interactive shell. This is expected.

If the local port is absent, confirm that the Windows proxy is running on port
7897, leave the current server shell, and reconnect with the explicit `-R`
option above. `ExitOnForwardFailure=yes` prevents a session from appearing
usable when the requested tunnel could not be installed. Do not change the
proxy to listen on `0.0.0.0` merely to repair this path; that would expose it
to other LAN users.
