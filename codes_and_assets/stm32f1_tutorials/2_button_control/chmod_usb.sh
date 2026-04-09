BUSDEV=$(lsusb | grep -i ST-LINK | awk '{print "/dev/bus/usb/"$2"/"substr($4,1,3)}')

if [ -z "$BUSDEV" ]; then
    echo "没有找到 ST-Link 设备，请先在 Windows 侧执行 usbipd attach"
    exit 1
fi

echo "找到 ST-Link 设备: $BUSDEV"
read -p "确认继续？"

sudo chmod 666 $BUSDEV
echo "权限已设置为 666"
