# tcu_logic 当前完成情况与联调验收清单

## 1. 当前模块总结（core/tcu_logic）
- 状态机主流程已闭环：`IDLE -> PREPARE -> STARTING -> CHARGING -> STOPPING -> STOPPED/ERROR`。
- 启停入口已支持 HMI 与 Platform 两路命令，且已联动 pile_controller 的 `event/data`。
- 故障处理已打通：`deviceErr_on` 进入故障流程，`deviceErr_off` 可触发恢复。
- 计费功能已落地：按 `HHMM` 时段模型解析，充电中累计分段电量/电费/服务费，并发布 `feeData`。
- 预充停机逻辑已实现：总金额接近预充阈值时触发 `tcu_stop_request` 并停机。
- 停止收敛逻辑已实现：`STOPPING` 下按“15秒或电量连续3次无变化”进入 `STOPPED`。
- 交易记录已接入日志系统：`STOPPING -> STOPPED` 时调用 `logTradeRecord(...)`。
- 会话清理已实现：进入 `IDLE` 自动清理充电流程变量，避免跨次污染。

## 2. 联调验收清单
1. 正常启动/充电/停止一轮，验证状态迁移与 `feeData` 连续性。
2. 启动失败路径（`start_response/start_complete` 失败）验证回退与事件。
3. 故障停机路径（`deviceErr_on`）验证进入 `STOPPING/ERROR` 与恢复。
4. 预充触发停机路径，验证 `tcu_stop_request` 与最终账单。
5. 拔枪停机路径，验证 `STOPPING` 收敛条件。
6. 重复命令/乱序命令，验证幂等性和状态不乱跳。
