#!/bin/sh

set -eu

usage() {
    echo "用法: $0 <run.sh路径>"
    echo "示例: $0 /usr/app/run.sh"
}

if [ "$#" -ne 1 ]; then
    usage
    exit 1
fi

RUN_SH="$1"

if [ ! -f "$RUN_SH" ]; then
    echo "错误: 文件不存在: $RUN_SH"
    exit 1
fi

BACKUP_FILE="${RUN_SH}.bak.$(date +%Y%m%d%H%M%S)"
TMP_FILE="${RUN_SH}.tmp.$$"

cp "$RUN_SH" "$BACKUP_FILE"

awk '
BEGIN {
    inserted = 0
    skip_block = 0
}

/^[[:space:]]*# PATCH_BEGIN: runtime services[[:space:]]*$/ {
    skip_block = 1
    next
}

/^[[:space:]]*# PATCH_END: runtime services[[:space:]]*$/ {
    skip_block = 0
    next
}

skip_block == 1 {
    next
}

/^[[:space:]]*cd[[:space:]]+\/usr\/test\/[[:space:]]*$/ {
    next
}

/^[[:space:]]*\.\/daemon&[[:space:]]*$/ {
    next
}

{
    print
}

/^[[:space:]]*ifconfig[[:space:]]+can1[[:space:]]+up[[:space:]]*$/ {
    print ""
    print "# PATCH_BEGIN: runtime services"
    print "cd /usr/bin"
    print "chmod 777 *"
    print "mosquitto -c /usr/app/config/mosquitto.conf -d"
    print ""
    print "cd /usr/app/tcu"
    print "chmod 777 *"
    print "./tcu_daemon &"
    print "# PATCH_END: runtime services"
    inserted = 1
}

END {
    if (inserted == 0) {
        exit 2
    }
}
' "$RUN_SH" > "$TMP_FILE" || {
    rm -f "$TMP_FILE"
    echo "错误: 未找到插入位置 'ifconfig can1 up'，未修改原文件"
    echo "备份文件: $BACKUP_FILE"
    exit 1
}

mv "$TMP_FILE" "$RUN_SH"
chmod +x "$RUN_SH"

echo "修改完成: $RUN_SH"
echo "备份文件: $BACKUP_FILE"
echo "可用以下命令检查结果:"
echo "grep -nE \"/usr/test|daemon&|mosquitto|tcu_daemon|PATCH_BEGIN|PATCH_END\" \"$RUN_SH\""
