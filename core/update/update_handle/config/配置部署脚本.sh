#!/bin/bash
# ============================================
# TCU 升级程序配置文件部署脚本
# ============================================
# 
# 用途：将配置文件部署到目标设备
# 使用方法：sudo ./配置部署脚本.sh
#
# ============================================

# 配置
CONFIG_SOURCE="tcu_update.conf.example"
CONFIG_TARGET="/usr/app/config/tcu_update.conf"
CONFIG_DIR="/usr/app/config"

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}===========================================${NC}"
echo -e "${GREEN}  TCU 升级程序配置文件部署${NC}"
echo -e "${GREEN}===========================================${NC}"
echo ""

# 检查是否为 root 用户
if [ "$EUID" -ne 0 ]; then 
    echo -e "${RED}错误: 请使用 root 权限运行此脚本${NC}"
    echo "使用方法: sudo $0"
    exit 1
fi

# 检查源文件是否存在
if [ ! -f "$CONFIG_SOURCE" ]; then
    echo -e "${RED}错误: 配置文件示例不存在: $CONFIG_SOURCE${NC}"
    exit 1
fi

# 创建目标目录
echo -e "${YELLOW}[1/4] 创建配置目录...${NC}"
if [ ! -d "$CONFIG_DIR" ]; then
    mkdir -p "$CONFIG_DIR"
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}  ✓ 目录创建成功: $CONFIG_DIR${NC}"
    else
        echo -e "${RED}  ✗ 目录创建失败: $CONFIG_DIR${NC}"
        exit 1
    fi
else
    echo -e "${GREEN}  ✓ 目录已存在: $CONFIG_DIR${NC}"
fi

# 检查目标文件是否存在
echo -e "${YELLOW}[2/4] 检查现有配置文件...${NC}"
if [ -f "$CONFIG_TARGET" ]; then
    echo -e "${YELLOW}  警告: 配置文件已存在: $CONFIG_TARGET${NC}"
    read -p "  是否覆盖现有配置文件? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo -e "${YELLOW}  已取消部署${NC}"
        exit 0
    fi
    # 备份现有配置文件
    BACKUP_FILE="${CONFIG_TARGET}.backup.$(date +%Y%m%d_%H%M%S)"
    cp "$CONFIG_TARGET" "$BACKUP_FILE"
    echo -e "${GREEN}  ✓ 已备份现有配置到: $BACKUP_FILE${NC}"
fi

# 复制配置文件
echo -e "${YELLOW}[3/4] 复制配置文件...${NC}"
cp "$CONFIG_SOURCE" "$CONFIG_TARGET"
if [ $? -eq 0 ]; then
    echo -e "${GREEN}  ✓ 配置文件复制成功${NC}"
else
    echo -e "${RED}  ✗ 配置文件复制失败${NC}"
    exit 1
fi

# 设置文件权限
echo -e "${YELLOW}[4/4] 设置文件权限...${NC}"
chmod 644 "$CONFIG_TARGET"
chown root:root "$CONFIG_TARGET"
if [ $? -eq 0 ]; then
    echo -e "${GREEN}  ✓ 文件权限设置成功${NC}"
else
    echo -e "${RED}  ✗ 文件权限设置失败${NC}"
    exit 1
fi

# 验证配置文件
echo ""
echo -e "${YELLOW}验证配置文件...${NC}"
if [ -f "$CONFIG_TARGET" ] && [ -r "$CONFIG_TARGET" ]; then
    echo -e "${GREEN}  ✓ 配置文件验证通过${NC}"
    echo ""
    echo -e "${GREEN}===========================================${NC}"
    echo -e "${GREEN}  部署完成！${NC}"
    echo -e "${GREEN}===========================================${NC}"
    echo ""
    echo "配置文件位置: $CONFIG_TARGET"
    echo "文件权限: $(ls -l "$CONFIG_TARGET" | awk '{print $1, $3, $4}')"
    echo ""
    echo -e "${YELLOW}提示:${NC}"
    echo "  1. 请根据实际需求编辑配置文件"
    echo "  2. 编辑命令: sudo vi $CONFIG_TARGET"
    echo "  3. 修改配置后需要重启程序才能生效"
else
    echo -e "${RED}  ✗ 配置文件验证失败${NC}"
    exit 1
fi

