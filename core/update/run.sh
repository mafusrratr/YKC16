#!/bin/sh
source base

#/opt/firewall.sh
ifconfig eth0 192.168.1.88
ifconfig eth1 192.168.2.88
#diald --apn=sctnet
cd /usr/test/
./daemon&

#lws.add 2021.09.13 
if [ ! -f /opt/tcu/lib/libtcu.so ];then
        echo "Don't have libtcu.so, cp /opt/backups_so/libtcu.so success"
	cp /opt/backups_so/libtcu.so /opt/tcu/lib/
fi

if [ ! -f /opt/tcu/lib/libtcu.so.2.02 ];then
        echo "Don't have libtcu.so.2.02, cp /opt/backups_so/libtcu.so.2.02 success"
	cp /opt/backups_so/libtcu.so.2.02 /opt/tcu/lib/
fi
#lws.add 2021.09.16
monitorled > /dev/null 2>&1 &

ip link set can0 type can bitrate 125000 restart-ms 100
ifconfig can0 up
ip link set can1 type can bitrate 125000 restart-ms 100
ifconfig can1 up

#tcu
sleep 10
#/mnt/nandflash/app/tcu_guarder &
#ln -s /dev/input/event1 /dev/event1
ifconfig eth0:1 192.168.1.107 netmask 255.255.255.0
route add -host test gw 192.168.1.1
./daemon&
route add default gw 192.168.1.1
echo nameserver 192.168.1.1>/tmp/resolv.conf
