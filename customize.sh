#!/system/bin/sh
SKIPUNZIP=0
ui_print "- CZN Speed (Zygisk)"
[ -f /data/adb/czn_speed.conf ] || echo "1.0" > /data/adb/czn_speed.conf
chmod 644 /data/adb/czn_speed.conf
ui_print "- Atur kecepatan: echo 2.0 > /data/adb/czn_speed.conf"
