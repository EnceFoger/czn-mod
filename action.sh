#!/system/bin/sh
CONF=/data/adb/czn_speed.conf
cur=$(cat $CONF 2>/dev/null)
case "$cur" in
  1.0) next=2.0;;
  2.0) next=3.0;;
  3.0) next=5.0;;
  5.0) next=10.0;;
  *) next=1.0;;
esac
echo "$next" > $CONF
chmod 644 $CONF
echo "Kecepatan: ${next}x"
