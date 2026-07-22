# access http/https server via browser

```
http://bsh-benchi
https://bsh-benchi
```

# curl (--> get HTTP 101 = Switching Protocols)

```
curl -i -N \
    -H "Connection: Upgrade" \
    -H "Upgrade: websocket" \
    --cacert ../src/webserver/certs/ca_cert.pem \
    wss://bsh-benchi
```

# install wscat

```
npm install -g wscat
```

# websocket (ws://)
```
# ressource / (netstats)
wscat --connect ws://bsh-benchi --ca ../src/webserver/certs/ca_cert.pem

# ressource /ws_echo
wscat --connect ws://bsh-benchi/ws_echo --ca ../src/webserver/certs/ca_cert.pem

```

# secure websocket (wss://)
```
# ressource / (netstats)
wscat --connect wss://bsh-benchi --ca ../src/webserver/certs/ca_cert.pem

# ressource /ws_echo
wscat --connect wss://bsh-benchi/ws_echo --ca ../src/webserver/certs/ca_cert.pem
```



# send websocket messages via script

```
http_proxy= https_proxy= ./send_websocket.py trigger '{"ll":[{"led":"on"}]}' --loop 1000

http_proxy= https_proxy= ./send_websocket.py trigger '{"ll":[{"led":"on"}]}' --loop 1000 --wss

```
