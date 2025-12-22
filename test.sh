#!/bin/sh
echo "module github.com/imchuncai/umem-cache" > go.mod
go get -t github.com/imchuncai/umem-cache-client-Go
if [ "$RAFT" == "0" ]; then						       \
	go test github.com/imchuncai/umem-cache-client-Go -timeout=1m	       \
	 -failfast -p=1 -v -run=TestClient -args $PWD/umem-cache $TLS 0;	       \
else									       \
	go test github.com/imchuncai/umem-cache-client-Go -timeout=1m	       \
	 -failfast -p=1 -v -run=TestCluster -args $PWD/umem-cache $TLS 0;	       \
fi
rm -f go.mod go.sum
