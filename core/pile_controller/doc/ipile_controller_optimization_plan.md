# IPileController 与 tcu2ccu_data 对齐优化方案（评审稿）

## 一、现状与问题

### 1.1 接口层 (ipile_controller.h) 与协议数据 (tcu2ccu_data.h) 的差异

| 能力 | 接口层 (IPileController) | 协议数据 (tcu2ccu_data.h) | 当前实现情况 |
|------|-------------------------|---------------------------|--------------|
| **启动充电** | `ChargeStartParams`: gunNo, chargeMode, targetVoltage, targetCurrent, targetEnergy, timeLimit | `TCU2CCU_CmdStartChargeData`: loadControlSwitch, plugAndChargeFlag, auxPowerVoltage | 接口参数被忽略，协议层用硬编码默认值 |
| **停止充电** | `ChargeStopParams`: gunNo, stopReason | `TCU2CCU_CmdStopChargeData`: stopReason, tcuStopCode | 仅用 stopReason=0x01，未使用 params |
| **状态查询** | `getStatus(gunNo, PileStatus*)`，单一体 PileStatus（电压/电流/功率/电量/工作状态/故障码/SOC/温度） | YC20（遥测）、YX22/YX23（遥信位）、StartComplete/StopComplete 等 | getAggregatedStatus 仅从 YC20 填部分字段，YX22 等未纳入 |
| **故障码** | `getFaultCode(gunNo, uint16_t*)` 单一故障码 | YX22/YX23 多位故障/告警位 | 协议无“单一故障码”，需约定聚合规则或改接口 |
| **清除故障** | `clearFault(gunNo)` 无故障码参数 | `TCU2CCU_CmdClearFaultData`: faultCode | 协议需要 faultCode，接口未提供 |
| **配置阶段** | 无 | 版本校验(0x07/0x08)、下发充电参数(0x09/0x0A) | 仅 CANPileController 扩展方法，未进接口 |
| **启动/停止应答与事件** | 仅 startCharge/stopCharge 返回值 | StartChargeResponse(confirmFlag, startFailReason)、StopChargeResponse、StartComplete/StopComplete 长帧数据 | 应答详情、失败原因、StartComplete/StopComplete 无法通过接口暴露 |

### 1.2 根因简述

- 接口按“通用充电桩抽象”设计（目标电压/电流/电量、单一状态、单一故障码），与当前 TCU–CCU CAN 协议按“命令帧/状态帧/数据帧”的划分不一致。
- 协议侧已有完整的 tcu2ccu_data 定义，接口侧仍使用自有的 PileStatus/ChargeStartParams/ChargeStopParams，导致：
  - 业务层拿不到协议级应答与事件（如失败原因、启动完成 BMS 信息）；
  - 协议层无法利用接口参数（如启动时的负荷开关、即插即充、辅助电源）；
  - 状态与故障需在 controller 里做“二次聚合”，且聚合规则不清晰。

---

## 二、优化方向（三选一或组合）

### 方案 A：接口向协议靠拢（推荐）

**思路**：IPileController 的入参/出参尽量直接使用 tcu2ccu_data.h 中的业务结构体，接口只做“透传 + 枪号/多通道”，不引入另一套抽象模型。

**具体建议**：

1. **启动充电**
   - 接口改为：`int startCharge(const TCU2CCU_CmdStartChargeData* cmd)`，或保留 `ChargeStartParams` 但在其内/外明确“仅用于 CAN 时映射到 TCU2CCU_CmdStartChargeData”。
- 若保留 ChargeStartParams：在接口头文件中注明“CAN 实现时映射为 loadControlSwitch/plugAndChargeFlag/auxPowerVoltage，后续参数由规约步骤下发”。

2. **停止充电**
   - 接口改为：`int stopCharge(const TCU2CCU_CmdStopChargeData* cmd)`，或让 `ChargeStopParams` 包含 stopReason + tcuStopCode，并在协议层使用。

3. **状态与数据**
   - 不再用单一的 `getStatus(gunNo, PileStatus*)` 覆盖所有需求，改为按协议数据分类提供：
     - `getYC20Data(TCU2CCU_DataYC20* out)` / `isYC20DataValid()`
     - `getYX22Data(TCU2CCU_DataYX22* out)` / `isYX22DataValid()`
     - `getYX23Data(TCU2CCU_DataYX23* out)` / `isYX23DataValid()`
     - 可选：保留 `getAggregatedStatus(PileStatus*)` 作为“简化视图”，明确由 YC20+YX22 等聚合，文档约定字段来源与缺失时的含义。

4. **应答与事件**
   - 在接口中增加“协议级结果”的只读访问（或回调）：
     - 启动应答：`isStartChargeResponseValid()` + `getStartChargeResponse(TCU2CCU_StartChargeResponseData*)`
     - 停止应答：同上，StopChargeResponse。
     - 启动完成/停止完成：`isStartCompleteDataValid()` + `getStartCompleteData(...)`，`getStopCompleteData(...)`（若业务需要）。
   - 这样业务层可根据 confirmFlag、startFailReason 等做提示和重试，无需再猜返回值。

5. **配置阶段**
   - 将“版本校验 + 下发充电参数”纳入接口，例如：
     - `int sendVersionCheck()` / `bool isVersionCheckResponseValid()` / `getVersionCheckResponse(TCU2CCU_VersionCheckResponseData*)`
     - `int sendChargeParams(const TCU2CCU_CmdChargeParamData* params)` / `bool isChargeParamResponseValid()` / `getChargeParamResponse(TCU2CCU_ChargeParamResponseData*)`
   - 实现层（如 CANPileController）内部仍调用现有 protocol 的 encode/decode，仅做转发。

6. **故障与清除故障**
   - `getFaultCode`：要么改为“从 YX22/YX23 聚合出一个主故障码”并文档化规则，要么改为 `getYX22Data`/`getYX23Data` 由上层按位解析，接口不再提供单一 faultCode。
   - `clearFault`：接口改为 `clearFault(uint8_t gunNo, uint16_t faultCode)`，与 TCU2CCU_CmdClearFaultData 一致；若无故障码可传 0 或协议约定值。

7. **头文件依赖**
   - ipile_controller.h 需包含或前向引用 tcu2ccu_data.h（或把 tcu2ccu_data 中接口用到的类型放到 ipile_controller.h 的注释/附录），避免业务层只包含 ipile_controller.h 时不知道协议结构体。

**优点**：与现有协议和测试程序一致，业务层可直接使用协议文档中的含义，扩展新帧时只需加 get/is 方法。  
**缺点**：接口与“某一种协议”绑定，若未来要做纯抽象多协议，需要再包一层适配。

---

### 方案 B：保留通用接口，在 Controller 内做映射层

**思路**：IPileController 保持现有 PileStatus/ChargeStartParams/ChargeStopParams 不变，在 CANPileController（及 protocol）内完成与 tcu2ccu_data 的双向映射。

**具体建议**：

1. **启动/停止**
   - 在 CANPileController::startCharge 中，根据 ChargeStartParams 填写 TCU2CCU_CmdStartChargeData（例如 chargeMode → loadControlSwitch/plugAndChargeFlag，或约定默认值表）。
   - 在 CANPileController::stopCharge 中，将 params->stopReason 映射到 stopReason，并约定 tcuStopCode 的默认或从 params 扩展字段读取。

2. **状态**
   - 在 getAggregatedStatus 中完善：从 YC20 + YX22（+ YX23）汇总到 PileStatus（workStatus 从 YX22.workStatus 来，faultCode 从 YX22/23 位拼成一个或主故障码），并在头文件或 ARCHITECTURE 中写清映射表。

3. **应答与事件**
   - 不增加接口 API，而是：
     - startCharge/stopCharge 内部轮询等待应答（或超时），把 confirmFlag、startFailReason 转为返回值或错误码；
     - onStatusChanged 在收到 StartComplete/StopComplete 或 YC20 更新时，用聚合后的 PileStatus 回调。

4. **配置**
   - 不加入 IPileController，仍由 CANPileController 的扩展方法提供；业务/测试在“使用 CAN 时”先调 encodeVersionCheck/encodeIssueChargeParams，再调 startCharge 等。

5. **故障**
   - getFaultCode：在 controller 内从 YX22/YX23 按约定规则合成一个 uint16_t（例如高字节 YX22 主故障、低字节 YX23）。
   - clearFault：接口保持 clearFault(gunNo)，内部传 0 或从某处缓存“当前故障码”调用协议。

**优点**：接口保持协议无关，上层只认 PileStatus/ChargeStartParams。  
**缺点**：映射规则复杂且易丢信息（失败原因、StartComplete 详细数据等）；每增加一种协议都要写一套映射。

---

### 方案 C：分层接口（抽象层 + 协议扩展层）

**思路**：保留现有 IPileController 为“最小公共抽象”（仅 init/cleanup、startCharge、stopCharge、heartbeat、可选 getStatus），另加“协议扩展接口”，由 CAN 实现同时实现扩展接口，业务层按需强转使用。

**具体建议**：

1. 定义 `IPileControllerCAN`（或 `IPileControllerProtocol`）：
   - 继承 IPileController；
   - 声明 getYX22Data、getYC20Data、encodeVersionCheck、encodeIssueChargeParams、getStartChargeResponse、isStartCompleteDataValid 等与 tcu2ccu_data 直接对应的方法。

2. IPileController 中：
   - startCharge(ChargeStartParams*)、stopCharge(ChargeStopParams*) 保留，但文档注明“CAN 实现时仅使用其中部分字段并映射到协议”；
   - getStatus 仍返回 PileStatus（聚合视图），可选。

3. 业务/测试代码：
   - 若只做启停和心跳，用 IPileController 即可；
   - 若需要版本校验、桩参数、YX22、启动应答详情等，则 `static_cast<IPileControllerCAN*>(controller)` 后调用扩展接口。

**优点**：兼容现有调用方，同时给需要协议细节的调用方一条清晰路径。  
**缺点**：两套类型并存（PileStatus vs TCU2CCU_*），扩展接口与 IPileController 的入参仍可能不一致，需在文档中说明。

---

## 三、推荐结论与实施顺序

- **更推荐方案 A（接口向协议靠拢）**：当前主流程就是 CAN + tcu2ccu_data，接口与协议统一可减少重复结构和歧义，便于联调与排障。若后续真有其它通信方式，再在 A 之上加一层薄适配即可。

**建议实施顺序**（在采纳 A 的前提下）：

1. **阶段 1（不破坏现有调用）**
   - 在 ipile_controller.h 中增加可选/扩展方法（如 getYX22Data、getStartChargeResponse、sendVersionCheck、sendChargeParams），参数与返回使用 tcu2ccu_data 类型；ipile_controller.h 包含或前向声明 tcu2ccu_data.h。
   - CANPileController 实现这些方法并转发到 protocol。
   - 保持 startCharge(ChargeStartParams*)、stopCharge(ChargeStopParams*)、getStatus(PileStatus*) 暂时不变，但在实现内用 tcu2ccu_data 做转换（并补全 stopReason/tcuStopCode 的映射）。

2. **阶段 2（接口统一）**
   - 将 startCharge 改为接受 `TCU2CCU_CmdStartChargeData*`（或保留 ChargeStartParams 但类型别名/兼容 TCU2CCU_CmdStartChargeData）。
   - 将 stopCharge 改为接受 `TCU2CCU_CmdStopChargeData*` 或扩展 ChargeStopParams。
   - 增加 getStartChargeResponse / getStopChargeResponse / getStartCompleteData / getStopCompleteData（按需），clearFault(gunNo, faultCode)。
   - 文档明确 getStatus 的 PileStatus 为 YC20+YX22 的聚合视图，并完善 getAggregatedStatus 对 YX22 的填充。

3. **阶段 3（可选）**
   - 若不再需要“通用抽象”，可移除 PileStatus/ChargeStartParams/ChargeStopParams，全部改为 tcu2ccu_data 类型；或保留 PileStatus 仅作“展示用简化视图”。

---

## 四、需要你拍板的点

1. **是否接受接口与 tcu2ccu_data 绑定？**  
   - 是 → 倾向方案 A。  
   - 否 → 倾向方案 B 或 C。

2. **配置流程（版本校验、下发桩参数）是否纳入 IPileController？**  
   - 纳入 → 接口加 sendVersionCheck、sendChargeParams 等。  
   - 不纳入 → 保持 CANPileController 扩展方法，由调用方在初始化流程里显式调用。

3. **getFaultCode / clearFault 的最终形态**：  
   - 单一故障码 + 文档约定聚合规则，还是改为直接暴露 YX22/YX23 由上层解析？clearFault 是否必须带 faultCode 参数？

4. **PileStatus 的保留与否**：  
   - 仅作“简化视图”（由 YC20+YX22 聚合），还是逐步废弃，业务层统一用 YC20/YX22/YX23？

你确认方向后，可按上述阶段拆成具体任务（改哪些头/实现/调用点）再落代码。

---

## 五、选定方案：通用接口聚焦业务 + 子类自定参数配置（已采纳）

### 5.1 设计思路

- **与 tcu2ccu_data 对齐**：出参/数据查询使用 tcu2ccu_data.h 中的结构体，保证与协议一致。
- **通用接口只做业务**：不暴露协议细节参数，提供“标准无参/少参”的启停、结果查询、状态与遥测遥信数据更新查询。
- **参数配置在子类**：版本校验、桩参数、启动选项（负荷开关、即插即充、辅助电源）、停止原因/故障码等，由继承类（如 CANPileController）自行提供配置入口，业务层在需要时通过具体类型调用。

这样既保持接口简洁、协议无关，又能在实现层和数据结构上与 tcu2ccu_data 完全对齐。

### 5.2 基类接口：仅三种标准函数（已采纳）

**IPileController 基类只包含以下三种标准能力，其余均由各子类按需实现。**

1. **充电启动**  
   - `int startCharge()`  
   - 下发启动充电命令；成功发送返回 0。具体参数（负荷开关、即插即充等）由子类在内部或通过自身配置接口设定。

2. **充电停止**  
   - `int stopCharge()`  
   - 下发停止充电命令；成功发送返回 0。停止原因等由子类内部或自身配置接口设定。

3. **获取充电桩状态**  
   - `int getPileStatus(PileStatus* out)` 或 `int getStatus(uint8_t gunNo, PileStatus* status)`  
   - 获取当前充电桩状态（可由子类从 YC20/YX22 等聚合为统一视图，或按协议定义的结构体输出）。

**生命周期**（建议保留，否则无法完成控制器创建与释放）：

- `bool initialize(const char* commType, const char* config)`
- `void cleanup()`

**不放入基类的（由各具体类自己实现）**

- 心跳、版本校验、下发桩参数、启动/停止结果查询、遥测遥信（YC20/YX22/YX23）、启动完成/停止完成、设置充电参数、清除故障等，均由 CANPileController 或其它实现类按协议需要自行声明与实现。

### 5.3 子类（如 CANPileController）职责

- 实现基类三种标准接口：`startCharge()`、`stopCharge()`、`getPileStatus()`（或 `getStatus()`），以及 `initialize()`/`cleanup()`。
- 自行提供本协议所需的所有其它接口：版本校验、桩参数、心跳状态、启停应答、遥测遥信、启动完成/停止完成、设参、清故障等，与 tcu2ccu_data 对齐；业务层在需要时通过具体类型调用。

### 5.4 头文件与依赖

- 基类若使用 `PileStatus`，可保留在 ipile_controller.h 中定义（或与 tcu2ccu_data 对齐后共用一个简化视图结构体）；若基类不依赖 tcu2ccu_data，则 ipile_controller.h 可不包含 tcu2ccu_data.h，由子类头文件自行包含。

### 5.5 小结

- **基类**：仅包含 充电启动、充电停止、获取充电桩状态 三种标准函数（+ initialize/cleanup）。
- **具体类**：各自实现上述三种，并扩展本协议需要的全部能力（心跳、配置、结果查询、遥测遥信等），与 tcu2ccu_data 完全对齐。

---

## 六、实施方案

### 6.1 目标与范围

- **目标**：基类 IPileController 仅保留 充电启动、充电停止、获取充电桩状态 三种标准函数 + initialize/cleanup；其余能力由各子类自行实现并对外提供。
- **范围**：ipile_controller.h、ipile_protocol.h、CANPileController、PileControllerProcess、测试程序及规约实现 CAN2CCUProtocol。

### 6.2 阶段一：基类接口收缩

| 序号 | 文件 | 修改内容 |
|------|------|----------|
| 1.1 | **ipile_controller.h** | ① 保留 `PileStatus` 结构体（getStatus 出参）。② 删除 `ChargeStartParams`、`ChargeStopParams`（启停不再通过基类传参）。③ 删除虚函数：`setChargeParams`、`getFaultCode`、`clearFault`、`heartbeat`、`onStatusChanged`。④ 将 `startCharge(const ChargeStartParams* params)` 改为 `startCharge()`（无参）。⑤ 将 `stopCharge(const ChargeStopParams* params)` 改为 `stopCharge()`（无参）。⑥ 保留 `getStatus(uint8_t gunNo, PileStatus* status)`，注释可改为“获取充电桩状态”。⑦ 保留 `initialize`、`cleanup`。 |
| 1.2 | **ipile_protocol.h** | ① 将 `encodeStartCharge(const ChargeStartParams* params)` 改为无参 `encodeStartCharge()`（或保留带参版本仅给 CAN 内部使用，接口层只声明无参）。② 将 `encodeStopCharge(const ChargeStopParams* params)` 改为无参 `encodeStopCharge()`。③ 其余 `encodeGetStatus`、`encodeClearFault`、`encodeHeartbeat`、`decodeFrame`、`getAggregatedStatus`、`decodeHeartbeat` 保留在规约层，供 CAN 等实现使用，不要求基类控制器暴露。 |

### 6.3 阶段二：CAN 控制器与规约实现适配

| 序号 | 文件 | 修改内容 |
|------|------|----------|
| 2.1 | **can_pile_controller.h** | ① 重写基类虚函数声明：`startCharge()`、`stopCharge()` 无参；`getStatus(uint8_t, PileStatus*)` 保留；移除对已删基类接口的 override（`getFaultCode`、`clearFault`、`heartbeat`、`onStatusChanged`）。② 将上述能力改为 CANPileController 自身公有方法（非 override）：`getFaultCode`、`clearFault`、`heartbeat` 等，供业务/测试通过具体类型调用。③ 保留现有扩展方法：`getYX22Data`、`isYX22DataValid`、`getHeartbeatCommStatus`、`isStartCompleteDataValid`、`encodeVersionCheck`、`isVersionCheckResponseValid`、`encodeIssueChargeParams`、`isChargeParamResponseValid`。 |
| 2.2 | **can_pile_controller.cpp** | ① `startCharge()`：无参，内部调用 `m_protocol->encodeStartCharge(nullptr)` 或规约层无参 `encodeStartCharge()`，使用协议默认/内部已配置的启动参数。② `stopCharge()`：无参，同上，使用默认停止原因。③ `getStatus()`：实现不变，仍从 protocol 的 getAggregatedStatus 或缓存填充 PileStatus。④ 原 `getFaultCode`、`clearFault`、`heartbeat`、`onStatusChanged` 改为普通成员函数（不再 override），实现逻辑可保持不变。⑤ 若规约层改为无参 encode，则 protocol 的 `encodeStartCharge()`/`encodeStopCharge()` 内部使用成员变量（如 m_cmdStartChargeData）的默认值或子类预先设置的配置。 |
| 2.3 | **can2ccu_protocol.h** | ① 若规约接口改为无参：声明 `encodeStartCharge()`、`encodeStopCharge()` 无参重载，或保留原签名供 CAN 内部使用、对外由 controller 只调无参。② 与 ipile_protocol.h 保持一致（无参虚函数则 CAN2CCUProtocol 实现无参版本）。 |
| 2.4 | **can2ccu_protocol.cpp** | ① 实现无参 `encodeStartCharge()`：内部使用默认或已设置的 m_cmdStartChargeData（如 loadControlSwitch=0x02, plugAndChargeFlag=0x01, auxPowerVoltage=0x0C）调用现有 encodeStartChargeFrame()。② 实现无参 `encodeStopCharge()`：内部使用默认 m_cmdStopChargeData（如 stopReason=0x01, tcuStopCode=0x00）调用 encodeStopChargeFrame()。③ 若保留带参版本给扩展用，可保留原 `encodeStartCharge(const ChargeStartParams*)` 为内部或已废弃，避免与无参版本重复实现。 |

### 6.4 阶段三：进程与调用方适配

| 序号 | 文件 | 修改内容 |
|------|------|----------|
| 3.1 | **pile_controller_process.cpp** | ① **心跳**：基类不再提供 `heartbeat()`。可选方案两选一或组合：**方案 A**：CANPileController 在内部（如 receiveThread 或独立定时）按 1s 周期自行发送心跳，process 不再调用 heartbeat；**方案 B**：process 中按具体类型调用，例如 `if (auto* can = dynamic_cast<CANPileController*>(m_controllers[i].get())) can->heartbeat();`。推荐方案 A，进程只依赖基类三接口 + init/cleanup。② **状态**：继续使用 `getStatus(gunNo, &status)`，无需修改。③ **onStatusChanged**：若 process 此前通过基类注册回调，删除该依赖；若 CAN 实现内部需要上报状态变化，可在 CANPileController 内用观察者或回调注册给 process，由 process 以具体类型注册，不放在基类。 |
| 3.2 | **test_start_stop_charge.cpp** | ① 启停调用改为无参：`controller.startCharge()`、`controller.stopCharge()`，删除 ChargeStartParams/ChargeStopParams 的构造与传递。② 心跳：当前已使用 CANPileController 类型，保留 `controller.heartbeat()` 调用（作为 CAN 扩展接口）。③ 版本校验、桩参数、YX22、启动完成等仍通过 `controller.encodeVersionCheck()`、`controller.isVersionCheckResponseValid()` 等 CAN 扩展接口调用，无需改动。 |
| 3.3 | **test_can_controller.cpp** | ① 已使用 CANPileController 类型，保留对 `getStatus`、`getFaultCode`、`heartbeat` 的调用；这些改为 CANPileController 自身方法，不再通过基类指针调用（若测试用基类指针，需改为具体类型或 dynamic_cast 后调用扩展接口）。 |
| 3.4 | **test_protocol.cpp** | ① 若存在对 ChargeStartParams/ChargeStopParams 的构造及 encodeStartCharge(params)/encodeStopCharge(params) 的调用，改为无参调用或使用规约层内部默认；若测试需要覆盖“带参启动/停止”，可改为直接调用 CAN2CCUProtocol 的带参内部接口或通过 CANPileController 的扩展配置接口设参后再 startCharge()/stopCharge()。 |

### 6.5 阶段四：清理与文档

| 序号 | 文件 | 修改内容 |
|------|------|----------|
| 4.1 | **ARCHITECTURE.md / ARCHITECTURE_UML.puml / ARCHITECTURE_UML_README.md** | 更新类图与说明：IPileController 仅保留 initialize、cleanup、startCharge()、stopCharge()、getStatus()；CANPileController 列出扩展方法（heartbeat、getYX22Data、encodeVersionCheck 等）。 |
| 4.2 | **ipile_controller_optimization_plan.md** | 保留本实施方案为记录；完成后可在文档末尾标注“已实施”及日期。 |

### 6.6 实施顺序与风险

- **推荐顺序**：阶段一（基类 + 规约接口）→ 阶段二（CAN 实现）→ 阶段三（进程与测试）→ 阶段四（文档）。
- **兼容性**：所有通过基类指针仅调用 startCharge/stopCharge/getStatus/initialize/cleanup 的代码可保持不变；依赖 heartbeat/getFaultCode/clearFault/onStatusChanged 的调用方必须改为使用具体类型（如 CANPileController）或改为由子类内部自行完成（如心跳在 controller 内部定时发送）。
- **回归**：完成各阶段后编译通过，运行 test_start_stop_charge、test_can_controller、test_protocol，确认启停与状态查询行为符合预期；进程主循环若采用方案 A（心跳在 CAN 内部），需确认 CAN 设备上心跳报文仍按 1s 周期发送。
