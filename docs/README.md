# Docs 索引（已整理）

本目录按“当前可用设计文档 + 历史重构文档”组织，不删除历史文件，避免信息丢失。

## 1. 当前评审入口
- `docs/SOFTWARE_OVERVIEW_DESIGN.md`  
  充电计费单元软件概要设计方案（模块组成、接口、信息流、扩展策略）。

## 2. 模块级设计文档（源码目录内）
- `core/pile_controller/CAN2CCU/doc/ARCHITECTURE.md`
- `core/pile_controller/CAN2CCU/doc/MQTT_COMM_PROTOCOL.md`
- `core/tcu_logic/doc/ARCHITECTURE.md`
- `core/tcu_logic/doc/STATE_MACHINE.md`
- `core/tcu_logic/doc/MQTT_COMM_PROTOCOL.md`
- `core/tcu_logic/doc/FEE_LOGIC.md`
- `core/meter/doc/ARCHITECTURE.md`
- `core/comm/Zhongshihua2.0/doc/FRAME_TYPES_STATUS.md`

## 3. 历史重构资料（保留）
- `docs/ARCHITECTURE_DESIGN.md`
- `docs/REFACTOR_PLAN.md`
- `docs/REFACTOR_SUMMARY.md`
- `docs/DEVELOPMENT_PLAN.md`
- `docs/NEXT_STEPS.md`
- `docs/PROGRESS_UPDATE.md`
- `docs/V2G_vs_EVS_COMPARISON.md`
- `docs/libv2gshm_USAGE.md`

## 4. 使用建议
- 方案评审优先看：`docs/SOFTWARE_OVERVIEW_DESIGN.md`。
- 模块联调时，回到各模块 `core/*/doc` 查看字段与时序细节。
- 历史文档仅作为背景参考，不作为当前实现唯一依据。
