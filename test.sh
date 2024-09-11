#!/bin/sh
nohup ./umem-cache > /dev/null 2>&1 &
serverPID=$!
echo "module github.com/imchuncai/umem-cache" > go.mod
go get -t github.com/imchuncai/umem-cache-client-Go
go test github.com/imchuncai/umem-cache-client-Go -count=1 -failfast -p=1 -v
kill $serverPID
rm go.mod go.sum