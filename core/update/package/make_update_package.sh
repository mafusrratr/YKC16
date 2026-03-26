#!/bin/bash

######################################################################
# TCU升级包制作脚本（简化版）
# 功能：交互式制作TCU升级包
# 输出：install.tar.gz
# 清单格式：XML（manifest.xml）
# 固定配置：权限777，所有者root，备份true
######################################################################

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 默认配置
SOURCE_DIR="."
OUTPUT_DIR="."
PACKAGE_NAME="TCU System Update"
PACKAGE_DESCRIPTION=""
OUTPUT_FILE="install.tar.gz"
TEMP_DIR="install"
MANIFEST_FILE="manifest.xml"
CHECKSUM_FILE="checksum.md5"

# 固定配置
PERMISSION="777"
OWNER="root"
BACKUP="true"

# 文件列表
declare -a FILES
declare -a DESTINATIONS
declare -a MD5_VALUES
declare -a FILE_SIZES

# 文件夹映射（使用两个数组：文件夹路径数组和目标路径前缀数组）
declare -a FOLDER_PATHS
declare -a FOLDER_PREFIXES

# 命令列表
declare -a PRE_UPDATE_CMDS
declare -a POST_UPDATE_CMDS
declare -a ROLLBACK_CMDS

######################################################################
# 函数：显示帮助信息
######################################################################
show_help() {
    cat << EOF
用法: $0 [选项]

选项:
  -s, --source DIR     源文件目录（默认：当前目录）
  -h, --help           显示帮助信息

说明:
  - 输出文件固定为: ${OUTPUT_FILE}（在当前目录）
  - 清单文件格式: XML（${MANIFEST_FILE}）
  - 固定配置: 权限777, 所有者root, 备份true
  - 交互式输入每个文件的目标路径

示例:
  $0                    # 使用当前目录作为源目录
  $0 -s ./source       # 指定源目录

EOF
}

######################################################################
# 函数：解析命令行参数
######################################################################
parse_args() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            -s|--source)
                SOURCE_DIR="$2"
                shift 2
                ;;
            -h|--help)
                show_help
                exit 0
                ;;
            *)
                echo -e "${RED}错误: 未知参数 $1${NC}"
                show_help
                exit 1
                ;;
        esac
    done

    # 转换为绝对路径
    SOURCE_DIR=$(cd "$SOURCE_DIR" && pwd)
    OUTPUT_DIR=$(pwd)
}

######################################################################
# 函数：检查依赖
######################################################################
check_dependencies() {
    local missing=0

    if ! command -v md5sum &> /dev/null && ! command -v md5 &> /dev/null; then
        echo -e "${RED}错误: 需要 md5sum 或 md5 命令${NC}"
        missing=1
    fi

    if ! command -v tar &> /dev/null; then
        echo -e "${RED}错误: 需要 tar 命令${NC}"
        missing=1
    fi

    if [[ $missing -eq 1 ]]; then
        exit 1
    fi
}

######################################################################
# 函数：计算MD5（兼容不同系统）
######################################################################
calc_md5() {
    local file="$1"
    if command -v md5sum &> /dev/null; then
        md5sum "$file" | awk '{print $1}'
    elif command -v md5 &> /dev/null; then
        md5 -q "$file"
    else
        echo ""
    fi
}

######################################################################
# 函数：扫描源目录文件
######################################################################
scan_source_files() {
    echo -e "${BLUE}扫描源目录: ${SOURCE_DIR}${NC}"
    
    local count=0
    while IFS= read -r -d '' file; do
        local rel_path="${file#$SOURCE_DIR/}"
        
        # BY ZF: 跳过.DS_Store文件（Mac系统文件）
        if [[ "$(basename "$rel_path")" == ".DS_Store" ]]; then
            continue
        fi
        
        # BY ZF: 跳过source根目录下的直接文件，只处理子文件夹下的文件
        local dir_part=$(dirname "$rel_path")
        if [[ "$dir_part" == "." ]]; then
            continue
        fi
        
        FILES+=("$rel_path")
        ((count++))
    done < <(find "$SOURCE_DIR" -type f -print0 2>/dev/null | sort -z)
    
    if [[ $count -eq 0 ]]; then
        echo -e "${RED}错误: 源目录中没有找到文件${NC}"
        exit 1
    fi
    
    echo -e "${GREEN}找到 ${count} 个文件${NC}"
    echo ""
}

######################################################################
# 函数：获取文件的文件夹路径
######################################################################
get_file_folder() {
    local file="$1"
    local dir=$(dirname "$file")
    if [[ "$dir" == "." ]]; then
        echo "."
    else
        echo "$dir"
    fi
}

######################################################################
# 函数：获取文件夹的目标路径前缀
######################################################################
get_folder_prefix() {
    local folder="$1"
    local i=0
    for f in "${FOLDER_PATHS[@]}"; do
        if [[ "$f" == "$folder" ]]; then
            echo "${FOLDER_PREFIXES[$i]}"
            return 0
        fi
        ((i++))
    done
    echo ""
}

######################################################################
# 函数：格式化文件大小
######################################################################
format_size() {
    local size=$1
    if [[ $size -lt 1024 ]]; then
        echo "${size}B"
    elif [[ $size -lt 1048576 ]]; then
        echo "$((size / 1024))KB"
    else
        echo "$((size / 1048576))MB"
    fi
}

######################################################################
# 函数：交互式输入包基本信息
######################################################################
input_package_info() {
    echo -e "${BLUE}===========================================${NC}"
    echo -e "${BLUE}  配置包基本信息${NC}"
    echo -e "${BLUE}===========================================${NC}"
    echo ""
    
    read -p "包名称 [${PACKAGE_NAME}]: " input_name
    if [[ -n "$input_name" ]]; then
        PACKAGE_NAME="$input_name"
    fi
    
    read -p "包描述 []: " input_desc
    if [[ -n "$input_desc" ]]; then
        PACKAGE_DESCRIPTION="$input_desc"
    fi
    
    echo ""
}

######################################################################
# 函数：交互式输入文件夹目标路径
######################################################################
input_file_destinations() {
    echo -e "${BLUE}===========================================${NC}"
    echo -e "${BLUE}  配置文件映射${NC}"
    echo -e "${BLUE}===========================================${NC}"
    echo ""
    
    # 收集所有文件夹（使用普通数组）
    declare -a folder_set
    for file in "${FILES[@]}"; do
        local folder=$(get_file_folder "$file")
        local found=0
        for f in "${folder_set[@]}"; do
            if [[ "$f" == "$folder" ]]; then
                found=1
                break
            fi
        done
        if [[ $found -eq 0 ]]; then
            folder_set+=("$folder")
        fi
    done
    
    # 按文件夹排序
    local sorted_folders=($(printf '%s\n' "${folder_set[@]}" | sort))
    
    echo -e "${BLUE}检测到 ${#sorted_folders[@]} 个文件夹${NC}"
    echo ""
    
    # 为每个文件夹输入目标路径前缀
    for folder in "${sorted_folders[@]}"; do
        # 获取该文件夹下的文件列表
        local folder_files=()
        for file in "${FILES[@]}"; do
            local file_folder=$(get_file_folder "$file")
            if [[ "$folder" == "." ]]; then
                # 根目录文件（没有子目录的文件）
                if [[ -z "$file_folder" || "$file_folder" == "." ]]; then
                    folder_files+=("$file")
                fi
            else
                # 子目录文件
                if [[ "$file_folder" == "$folder" ]]; then
                    folder_files+=("$file")
                fi
            fi
        done
        
        local file_count=${#folder_files[@]}
        if [[ $file_count -eq 0 ]]; then
            continue
        fi
        
        local folder_display="$folder"
        if [[ "$folder" == "." ]]; then
            folder_display="根目录"
        fi
        
        echo -e "${YELLOW}[文件夹: ${folder_display}]${NC}"
        echo "  包含 ${file_count} 个文件:"
        for file in "${folder_files[@]}"; do
            echo "    - $(basename "$file")"
        done
        echo ""
        
        while true; do
            read -p "  请输入目标路径前缀（绝对路径，留空跳过此文件夹）: " dest_prefix
            
            if [[ -z "$dest_prefix" ]]; then
                echo -e "${YELLOW}  (已跳过此文件夹)${NC}"
                echo ""
                break
            fi
            
            # 验证绝对路径
            if [[ "$dest_prefix" != /* ]]; then
                echo -e "${RED}  错误: 目标路径必须是绝对路径（以/开头）${NC}"
                continue
            fi
            
            # 确保路径以/结尾（如果不是根目录）
            if [[ "$dest_prefix" != "/" && "${dest_prefix: -1}" != "/" ]]; then
                dest_prefix="${dest_prefix}/"
            fi
            
            # 保存文件夹映射（添加到数组）
            FOLDER_PATHS+=("$folder")
            FOLDER_PREFIXES+=("$dest_prefix")
            
            echo -e "${GREEN}  (权限: ${PERMISSION}, 所有者: ${OWNER}, 备份: ${BACKUP})${NC}"
            if [[ ${#folder_files[@]} -gt 0 ]]; then
                echo -e "${GREEN}  示例: $(basename "${folder_files[0]}") -> ${dest_prefix}$(basename "${folder_files[0]}")${NC}"
            fi
            echo ""
            break
        done
    done
    
    # BY ZF: 根据文件夹映射生成所有文件的目标路径
    # 同时创建文件索引映射，确保FILES和DESTINATIONS一一对应
    declare -a FILES_TO_PACKAGE
    for file in "${FILES[@]}"; do
        local folder=$(get_file_folder "$file")
        local filename=$(basename "$file")
        
        # 获取该文件夹的目标路径前缀
        local dest_prefix=$(get_folder_prefix "$folder")
        
        # 如果该文件夹有映射，生成目标路径
        if [[ -n "$dest_prefix" ]]; then
            local dest_path="${dest_prefix}${filename}"
            DESTINATIONS+=("$dest_path")
            FILES_TO_PACKAGE+=("$file")  # BY ZF: 保存对应的源文件路径
            
            # 获取文件大小
            local full_path="${SOURCE_DIR}/${file}"
            local size=$(stat -f%z "$full_path" 2>/dev/null || stat -c%s "$full_path" 2>/dev/null || echo "0")
            FILE_SIZES+=("$size")
        fi
    done
    
    # BY ZF: 用FILES_TO_PACKAGE替换FILES，确保索引一致
    FILES=("${FILES_TO_PACKAGE[@]}")
}

######################################################################
# 函数：交互式输入更新命令
######################################################################
input_commands() {
    echo -e "${BLUE}===========================================${NC}"
    echo -e "${BLUE}  配置更新命令（可选）${NC}"
    echo -e "${BLUE}===========================================${NC}"
    echo ""
    
    read -p "需要配置更新命令吗? [y/N]: " need_cmd
    if [[ "$need_cmd" != "y" && "$need_cmd" != "Y" ]]; then
        return
    fi
    
    echo ""
    echo "预更新命令（更新前执行，留空结束）:"
    local cmd_index=1
    while true; do
        read -p "  命令${cmd_index}: " cmd
        if [[ -z "$cmd" ]]; then
            break
        fi
        PRE_UPDATE_CMDS+=("$cmd")
        ((cmd_index++))
    done
    
    echo ""
    echo "后更新命令（更新后执行，留空结束）:"
    cmd_index=1
    while true; do
        read -p "  命令${cmd_index}: " cmd
        if [[ -z "$cmd" ]]; then
            break
        fi
        POST_UPDATE_CMDS+=("$cmd")
        ((cmd_index++))
    done
    
    echo ""
    echo "回滚命令（回滚时执行，留空结束）:"
    cmd_index=1
    while true; do
        read -p "  命令${cmd_index}: " cmd
        if [[ -z "$cmd" ]]; then
            break
        fi
        ROLLBACK_CMDS+=("$cmd")
        ((cmd_index++))
    done
    
    echo ""
}

######################################################################
# 函数：显示配置确认
######################################################################
show_config_confirm() {
    echo -e "${BLUE}===========================================${NC}"
    echo -e "${BLUE}  配置确认${NC}"
    echo -e "${BLUE}===========================================${NC}"
    echo ""
    
    echo "包信息:"
    echo "  名称: ${PACKAGE_NAME}"
    if [[ -n "$PACKAGE_DESCRIPTION" ]]; then
        echo "  描述: ${PACKAGE_DESCRIPTION}"
    fi
    echo "  文件数: ${#DESTINATIONS[@]}"
    echo ""
    
    echo "文件列表 (${#DESTINATIONS[@]}个文件):"
    local file_index=0
    for file in "${FILES[@]}"; do
        if [[ $file_index -lt ${#DESTINATIONS[@]} ]]; then
            local dest="${DESTINATIONS[$file_index]}"
            echo -e "  ${GREEN}✓${NC} ${file} -> ${dest} (${PERMISSION}, ${OWNER}, 备份)"
            ((file_index++))
        fi
    done
    echo ""
    
    if [[ ${#PRE_UPDATE_CMDS[@]} -gt 0 || ${#POST_UPDATE_CMDS[@]} -gt 0 || ${#ROLLBACK_CMDS[@]} -gt 0 ]]; then
        echo "更新命令:"
        if [[ ${#PRE_UPDATE_CMDS[@]} -gt 0 ]]; then
            echo "  预更新: ${PRE_UPDATE_CMDS[*]}"
        fi
        if [[ ${#POST_UPDATE_CMDS[@]} -gt 0 ]]; then
            echo "  后更新: ${POST_UPDATE_CMDS[*]}"
        fi
        if [[ ${#ROLLBACK_CMDS[@]} -gt 0 ]]; then
            echo "  回滚: ${ROLLBACK_CMDS[*]}"
        fi
        echo ""
    fi
}

######################################################################
# 函数：创建升级包目录结构
######################################################################
create_package_structure() {
    echo -e "${BLUE}正在生成升级包...${NC}"
    
    # 清理旧的临时目录
    if [[ -d "$TEMP_DIR" ]]; then
        rm -rf "$TEMP_DIR"
    fi
    
    # 创建目录结构
    mkdir -p "${TEMP_DIR}/files"
    
    echo -e "  ${GREEN}✓${NC} 创建目录结构"
}

######################################################################
# 函数：复制文件到升级包
######################################################################
copy_files() {
    local file_count=${#FILES[@]}
    local copied_count=0
    
    # BY ZF: FILES数组已经只包含有映射的文件，直接遍历即可
    for file_index in $(seq 0 $((file_count - 1))); do
        local file="${FILES[$file_index]}"
        
        # BY ZF: 再次检查，确保跳过.DS_Store文件
        if [[ "$(basename "$file")" == ".DS_Store" ]]; then
            continue
        fi
        
        local src_file="${SOURCE_DIR}/${file}"
        local dest_file="${TEMP_DIR}/files/${file}"
        
        # 检查源文件是否存在
        if [[ ! -f "$src_file" ]]; then
            echo -e "${YELLOW}  警告: 源文件不存在，跳过: ${src_file}${NC}"
            continue
        fi
        
        # 创建目标目录
        mkdir -p "$(dirname "$dest_file")"
        
        # 复制文件
        cp "$src_file" "$dest_file"
        
        ((copied_count++))
    done
    
    echo -e "  ${GREEN}✓${NC} 复制文件 (${copied_count}个文件)"
}

######################################################################
# 函数：计算文件MD5
######################################################################
calculate_md5() {
    # 初始化临时校验和文件
    > "${TEMP_DIR}/${CHECKSUM_FILE}.tmp"
    
    # BY ZF: 计算files目录下所有文件的MD5（跳过.DS_Store文件）
    while IFS= read -r -d '' file; do
        local rel_path="${file#${TEMP_DIR}/}"
        
        # BY ZF: 跳过.DS_Store文件
        if [[ "$(basename "$rel_path")" == ".DS_Store" ]]; then
            continue
        fi
        
        local md5_val=$(calc_md5 "$file")
        
        # 保存到校验和文件（临时）
        echo "${md5_val}  ${rel_path}" >> "${TEMP_DIR}/${CHECKSUM_FILE}.tmp"
    done < <(find "${TEMP_DIR}/files" -type f -print0 | sort -z)
    
    echo -e "  ${GREEN}✓${NC} 计算MD5校验和"
}

######################################################################
# 函数：生成XML清单文件
######################################################################
generate_manifest_xml() {
    local manifest_path="${TEMP_DIR}/${MANIFEST_FILE}"
    local build_date=$(date -u +"%Y-%m-%dT%H:%M:%S")
    
    # 开始XML文件
    cat > "$manifest_path" << EOF
<?xml version="1.0" encoding="UTF-8"?>
<package packageVersion="1.0">
  <packageInfo>
    <name>$(echo "$PACKAGE_NAME" | sed 's/&/\&amp;/g; s/</\&lt;/g; s/>/\&gt;/g')</name>
    <buildDate>${build_date}</buildDate>
    <description>$(echo "$PACKAGE_DESCRIPTION" | sed 's/&/\&amp;/g; s/</\&lt;/g; s/>/\&gt;/g')</description>
  </packageInfo>
  <files>
EOF
    
    # BY ZF: 生成文件列表（FILES和DESTINATIONS已经一一对应）
    local file_count=${#FILES[@]}
    for file_index in $(seq 0 $((file_count - 1))); do
        local file="${FILES[$file_index]}"
        
        # BY ZF: 跳过.DS_Store文件，不生成到manifest.xml中
        if [[ "$(basename "$file")" == ".DS_Store" ]]; then
            continue
        fi
        
        local dest="${DESTINATIONS[$file_index]}"
        local size="${FILE_SIZES[$file_index]}"
        local rel_path="files/${file}"
        
        # 从临时校验和文件中获取MD5（使用精确匹配）
        local md5_val=$(grep -F "  ${rel_path}" "${TEMP_DIR}/${CHECKSUM_FILE}.tmp" | awk '{print $1}')
        
        if [[ -z "$md5_val" ]]; then
            echo -e "${YELLOW}  警告: 无法获取文件MD5: ${rel_path}${NC}"
            md5_val=""
        fi
        
        cat >> "$manifest_path" << EOF
    <file>
      <source>${rel_path}</source>
      <destination>$(echo "$dest" | sed 's/&/\&amp;/g; s/</\&lt;/g; s/>/\&gt;/g')</destination>
      <permission>${PERMISSION}</permission>
      <owner>${OWNER}</owner>
      <backup>${BACKUP}</backup>
      <md5>${md5_val}</md5>
      <size>${size}</size>
    </file>
EOF
    done
    
    # 添加命令部分
    cat >> "$manifest_path" << EOF
  </files>
  <commands>
    <preUpdate>
EOF
    
    for cmd in "${PRE_UPDATE_CMDS[@]}"; do
        echo "      <command>$(echo "$cmd" | sed 's/&/\&amp;/g; s/</\&lt;/g; s/>/\&gt;/g')</command>" >> "$manifest_path"
    done
    
    cat >> "$manifest_path" << EOF
    </preUpdate>
    <postUpdate>
EOF
    
    for cmd in "${POST_UPDATE_CMDS[@]}"; do
        echo "      <command>$(echo "$cmd" | sed 's/&/\&amp;/g; s/</\&lt;/g; s/>/\&gt;/g')</command>" >> "$manifest_path"
    done
    
    cat >> "$manifest_path" << EOF
    </postUpdate>
    <rollback>
EOF
    
    for cmd in "${ROLLBACK_CMDS[@]}"; do
        echo "      <command>$(echo "$cmd" | sed 's/&/\&amp;/g; s/</\&lt;/g; s/>/\&gt;/g')</command>" >> "$manifest_path"
    done
    
    cat >> "$manifest_path" << EOF
    </rollback>
  </commands>
</package>
EOF
    
    echo -e "  ${GREEN}✓${NC} 生成清单文件 (${MANIFEST_FILE})"
}

######################################################################
# 函数：生成校验和文件
######################################################################
generate_checksum_file() {
    local checksum_path="${TEMP_DIR}/${CHECKSUM_FILE}"
    
    # 复制临时文件
    mv "${TEMP_DIR}/${CHECKSUM_FILE}.tmp" "$checksum_path"
    
    # 添加清单文件的MD5
    local manifest_path="${TEMP_DIR}/${MANIFEST_FILE}"
    local manifest_md5=$(calc_md5 "$manifest_path")
    echo "${manifest_md5}  ${MANIFEST_FILE}" >> "$checksum_path"
    
    echo -e "  ${GREEN}✓${NC} 生成校验和文件 (${CHECKSUM_FILE})"
}

######################################################################
# 函数：打包成tar.gz
######################################################################
create_package() {
    local output_path="${OUTPUT_DIR}/${OUTPUT_FILE}"
    
    # 如果输出文件已存在，删除
    if [[ -f "$output_path" ]]; then
        rm -f "$output_path"
    fi
    
    # 打包（在临时目录的父目录中执行，以便压缩包内包含install目录）
    local temp_parent=$(cd "$(dirname "$TEMP_DIR")" && pwd)
    local temp_name=$(basename "$TEMP_DIR")
    cd "$temp_parent"
    tar -czf "${OUTPUT_DIR}/${OUTPUT_FILE}" "$temp_name" 2>/dev/null || {
        cd - > /dev/null
        echo -e "${RED}错误: 打包失败${NC}"
        exit 1
    }
    cd - > /dev/null
    
    echo -e "  ${GREEN}✓${NC} 打包压缩 (${OUTPUT_FILE})"
}

######################################################################
# 函数：清理临时文件
######################################################################
cleanup() {
    if [[ -d "$TEMP_DIR" ]]; then
        rm -rf "$TEMP_DIR"
    fi
}

######################################################################
# 函数：验证升级包
######################################################################
verify_package() {
    echo ""
    echo -e "${BLUE}验证升级包...${NC}"
    
    local output_path="${OUTPUT_DIR}/${OUTPUT_FILE}"
    
    # 检查文件是否存在
    if [[ ! -f "$output_path" ]]; then
        echo -e "${RED}错误: 升级包文件不存在${NC}"
        return 1
    fi
    
    # 检查文件大小
    local file_size=$(stat -f%z "$output_path" 2>/dev/null || stat -c%s "$output_path" 2>/dev/null || echo "0")
    if [[ $file_size -eq 0 ]]; then
        echo -e "${RED}错误: 升级包文件大小为0${NC}"
        return 1
    fi
    
    # 测试解压
    local test_dir=$(mktemp -d)
    tar -xzf "$output_path" -C "$test_dir" 2>/dev/null || {
        echo -e "${RED}错误: 升级包无法解压${NC}"
        rm -rf "$test_dir"
        return 1
    }
    
    # 检查清单文件是否存在
    if [[ ! -f "${test_dir}/install/${MANIFEST_FILE}" ]]; then
        echo -e "${RED}错误: 清单文件不存在${NC}"
        rm -rf "$test_dir"
        return 1
    fi
    
    # 检查校验和文件是否存在
    if [[ ! -f "${test_dir}/install/${CHECKSUM_FILE}" ]]; then
        echo -e "${RED}错误: 校验和文件不存在${NC}"
        rm -rf "$test_dir"
        return 1
    fi
    
    # 清理测试目录
    rm -rf "$test_dir"
    
    echo -e "${GREEN}✓ 升级包验证通过${NC}"
    return 0
}

######################################################################
# 主函数
######################################################################
main() {
    # 解析参数
    parse_args "$@"
    
    # 检查依赖
    check_dependencies
    
    # 检查源目录
    if [[ ! -d "$SOURCE_DIR" ]]; then
        echo -e "${RED}错误: 源目录不存在: ${SOURCE_DIR}${NC}"
        exit 1
    fi
    
    # 显示标题
    echo -e "${BLUE}===========================================${NC}"
    echo -e "${BLUE}  TCU升级包制作工具（简化版）${NC}"
    echo -e "${BLUE}===========================================${NC}"
    echo ""
    
    # 扫描源文件
    scan_source_files
    
    # 输入包基本信息
    input_package_info
    
    # 输入文件目标路径
    input_file_destinations
    
    # 检查是否有文件
    if [[ ${#DESTINATIONS[@]} -eq 0 ]]; then
        echo -e "${RED}错误: 没有配置任何文件${NC}"
        exit 1
    fi
    
    # 输入更新命令
    input_commands
    
    # 显示配置确认
    show_config_confirm
    
    # 确认配置
    read -p "确认以上配置? [Y/n]: " confirm
    if [[ "$confirm" == "n" || "$confirm" == "N" ]]; then
        echo -e "${YELLOW}已取消${NC}"
        exit 0
    fi
    
    echo ""
    
    # 创建升级包结构
    create_package_structure
    
    # 复制文件
    copy_files
    
    # 计算MD5
    calculate_md5
    
    # 生成清单文件
    generate_manifest_xml
    
    # 生成校验和文件
    generate_checksum_file
    
    # 打包
    create_package
    
    # 清理临时文件
    cleanup
    
    # 验证升级包
    if verify_package; then
        echo ""
        echo -e "${GREEN}完成！升级包已生成: ${OUTPUT_DIR}/${OUTPUT_FILE}${NC}"
        
        # 显示文件大小
        local file_size=$(stat -f%z "${OUTPUT_DIR}/${OUTPUT_FILE}" 2>/dev/null || stat -c%s "${OUTPUT_DIR}/${OUTPUT_FILE}" 2>/dev/null || echo "0")
        local size_str=$(format_size $file_size)
        echo -e "${GREEN}包大小: ${size_str}${NC}"
    else
        echo -e "${RED}错误: 升级包验证失败${NC}"
        exit 1
    fi
}

# 执行主函数
main "$@"

