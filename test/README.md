# Protocol probes

Two Python scripts that speak the wire protocol by hand, without going through
the official client. They exist to check what a broken or hostile client can do
to a station, which the normal client never exercises.

Start a server on the test ports first:

```bash
printf '[General]\nbackendName=hamlib\nmodel=1\ntcpPort=17800\nudpPort=17801\npassword=secret\naudioIn=default\naudioOut=default\n' > /tmp/srv.ini
remoterig-server --headless --config /tmp/srv.ini
```

Then:

```bash
python3 test/protocol_probe.py    # authentication, framing, UDP forgery, second client
python3 test/deadman_probe.py     # client that vanishes silently while transmitting
```

```bash
python3 test/mode_probe.py        # the rig's answer wins over the request
```

```bash
python3 test/command_probe.py     # a burst of commands, and state still flowing
```

`protocol_probe.py` exits non-zero if any check fails, so it can go in a CI job.
`deadman_probe.py` takes about twenty seconds: it waits for the watchdog to fire.
