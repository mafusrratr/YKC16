#!/bin/sh

export LD_LIBRARY_PATH=/opt/lib

busybox ifconfig lo 127.0.0.1

busybox ifconfig eth0:1 192.168.1.202
#busybox ifconfig eth1 192.168.2.102
busybox ifconfig eth1 192.168.2.102
mount -t tmpfs -o size=10m tmpfs /opt/ramdisk 

if [ ! -d "/opt/ramdisk/log" ];then
    echo -e "new log \r"
    mkdir /opt/ramdisk/log
fi

if [ ! -d "/opt/ramdisk/update" ];then
    echo -e "new update \r"
    mkdir /opt/ramdisk/update
fi

echo -e "run hw \r\r"
/opt/app/hw

echo -e "disable eth0 eth1 \r\r"
/opt/ff.sh

sleep 6

echo -e "run pppd gprs \r\r"
#pppd call gprsdial &
/opt/app/tcu/tcu_ppp &

echo -e "run mon_main \r\r"
/opt/app/mon/mon_main &
sleep 3
cd /opt/app/hmi
nice -n 5 ./hmi_main &

cd /usr/app/tcu
chmod 777 *
./tcu_daemon &
