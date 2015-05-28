#!/usr/bin/env sh
# This script converts the cifar data into leveldb format.

EXAMPLE=examples/cifar10
DATA=data/cifar10
DBTYPE=lmdb
DBLENGTH=5000

echo "Creating $DBTYPE..."

#rm -rf $EXAMPLE/cifar10_train_$DBTYPE $EXAMPLE/cifar10_test_$DBTYPE

GLOG_logtostderr=1 ./build/examples/cifar10/ls_convert_cifar_data.bin $DATA $EXAMPLE $DBTYPE $DBLENGTH

echo "Done."
