#!/bin/sh

dev=sdd

if ( grep ${dev} /proc/mounts > /dev/null 2>&1 )
then
        echo Will not dd to a mounted fs.
        exit 1
fi

dd if=u-boot.bin of=/dev/${dev} bs=512 seek=2 skip=2 && sync
