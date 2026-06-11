const state = {
  connected: false,
  mqttClient: null,
  seq: 1,
  selectedGun: 0,
  logs: [],
  timeline: [],
  meterPaused: false,
  guns: [
    {
      name: "A枪",
      localGun: 0,
      connected: false,
      logicState: "UNKNOWN",
      pileState: "IDLE",
      mode: "charge",
      vin: "LJ21BABB8L1001955",
      voltage: 750,
      current: 80,
      soc: 45,
      targetSoc: 80,
      dischargeTargetSoc: 20,
      socRatePerMinute: 1,
      totalEnergy: 12.345,
      reverseEnergy: 0,
      fault: false,
      running: false,
      meterOnline: true,
      chargeMode: 1
    },
    {
      name: "B枪",
      localGun: 1,
      connected: false,
      logicState: "UNKNOWN",
      pileState: "IDLE",
      mode: "charge",
      vin: "LJ21BABB8L1001955",
      voltage: 750,
      current: 80,
      soc: 45,
      targetSoc: 80,
      dischargeTargetSoc: 20,
      socRatePerMinute: 1,
      totalEnergy: 8.5,
      reverseEnergy: 0,
      fault: false,
      running: false,
      meterOnline: true,
      chargeMode: 1
    }
  ]
};

const $ = (selector) => document.querySelector(selector);
const $$ = (selector) => Array.from(document.querySelectorAll(selector));

function nowMs() {
  return Date.now();
}

function topicPrefix() {
  return $("#topicPrefix").value || "tcu";
}

function brokerUrl() {
  const host = $("#brokerHost").value || "127.0.0.1";
  const port = $("#brokerPort").value || "18080";
  return `http://${host}:${port}`;
}

function biasNo() {
  return Number.parseInt($("#biasNo").value || "0", 10) || 0;
}

function globalGun(gun) {
  return gun.localGun + biasNo();
}

function gunByGlobalGun(gunNo) {
  return state.guns.find((gun) => globalGun(gun) === gunNo) || null;
}

function nextSeq() {
  return state.seq++;
}

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function round3(value) {
  return Math.round(value * 1000) / 1000;
}

function powerKw(gun) {
  if (!gun.running) return 0;
  return Number(gun.voltage) * Number(gun.current) / 1000;
}

function remainMinutes(gun) {
  if (gun.socRatePerMinute <= 0) return 0;
  const raw = gun.mode === "discharge"
    ? (gun.soc - gun.dischargeTargetSoc) / gun.socRatePerMinute
    : (gun.targetSoc - gun.soc) / gun.socRatePerMinute;
  return Math.max(0, Math.round(raw));
}

function rootPayload(gun, source, extra) {
  return {
    ts: nowMs(),
    seq: nextSeq(),
    source,
    gun: globalGun(gun),
    ...extra
  };
}

function pileData(gun, type, data) {
  return rootPayload(gun, "pile_controller", { type, data });
}

function pileEvent(gun, type, data) {
  return rootPayload(gun, "pile_controller", { type, data });
}

function meterData(gun) {
  return rootPayload(gun, "tcu_meter", {
    data: {
      totalEnergy: round3(gun.totalEnergy),
      ReverseEnergy: round3(gun.reverseEnergy),
      voltage: Number(gun.voltage),
      current: Number(gun.current)
    }
  });
}

function meterEvent(gun, event, reason) {
  return rootPayload(gun, "tcu_meter", {
    event,
    data: { reason }
  });
}

function logicEvent(gun, event, data) {
  return rootPayload(gun, "tcu_logic", {
    event,
    data
  });
}

function showPreview(direction, topic, payload) {
  $("#previewDirection").textContent = direction;
  $("#previewDirection").className = direction === "RX" ? "badge" : "badge ok";
  $("#previewTopic").textContent = topic;
  $("#previewPayload").textContent = JSON.stringify(payload, null, 2);
}

// BY ZF: WebUI only calls the local C++ backend; MQTT is owned by backend.
function mqttReady() {
  return state.connected;
}

function publishToBroker(topic, payload) {
  void topic;
  void payload;
}

function subscribeRuntimeTopics() {
  pushTimeline("后端负责 MQTT", "Web 不直接订阅 broker");
}

function setBrokerConnected(connected, detail) {
  state.connected = connected;
  $("#connectBtn").textContent = connected ? "断开" : "连接";
  $("#connectionBadge").textContent = connected ? "已连接" : "未连接";
  $("#connectionBadge").className = connected ? "badge ok" : "badge muted";
  renderSummary();
  if (detail) {
    pushTimeline(connected ? "Broker 已连接" : "Broker 已断开", detail);
  }
}

function connectBroker() {
  const url = brokerUrl();
  fetch(`${url}/api/status`)
    .then((resp) => resp.json())
    .then((data) => {
      setBrokerConnected(Boolean(data.ok), `${url} -> ${data.mqttHost}:${data.mqttPort}`);
      subscribeRuntimeTopics();
    })
    .catch((err) => {
      setBrokerConnected(false, url);
      pushTimeline("后端连接失败", err.message || String(err));
    });
}

function disconnectBroker() {
  setBrokerConnected(false, brokerUrl());
}

function parseJsonPayload(payloadText) {
  try {
    return JSON.parse(payloadText);
  } catch (err) {
    return { raw: payloadText };
  }
}

function updateFromPileCmd(gun, payload) {
  if (!gun || !payload || !payload.cmd) {
    return;
  }
  const data = payload.data || {};
  if (payload.cmd === "start_charge") {
    gun.pileState = "CMD_START";
    gun.mode = Number(data.v2g) !== 0 ? "discharge" : "charge";
  }
  if (payload.cmd === "stop_charge") {
    gun.pileState = "CMD_STOP";
  }
  if (payload.cmd === "power_ctrl" && data.maxChargePowerKw) {
    gun.current = Number(data.maxChargePowerKw) * 1000 / Math.max(1, Number(gun.voltage));
  }
  if (payload.cmd === "outputVA_ctrl") {
    gun.voltage = Number(data.demandVoltage || data.outputVoltage || data.voltage || gun.voltage);
    gun.current = Number(data.demandCurrent || data.outputCurrent || data.current || gun.current);
  }
  if (payload.cmd === "clear_fault") {
    gun.fault = false;
    gun.pileState = gun.connected ? "PLUGGED" : "IDLE";
  }
}

function updateFromLogicEvent(gun, payload) {
  if (!gun || !payload) {
    return;
  }
  const data = payload.data || {};
  gun.logicState = data.state || payload.state || payload.event || gun.logicState;
}

function handleIncomingMqtt(topic, payloadText) {
  const payload = parseJsonPayload(payloadText);
  const prefix = topicPrefix().replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  const pileCmdMatch = topic.match(new RegExp(`^${prefix}/pile/(\\d+)/cmd$`));
  const logicEventMatch = topic.match(new RegExp(`^${prefix}/logic/(\\d+)/event$`));
  const gunToken = pileCmdMatch ? pileCmdMatch[1] : logicEventMatch ? logicEventMatch[1] : null;
  const gunNo = gunToken === null ? NaN : Number(gunToken);
  const gun = Number.isFinite(gunNo) ? gunByGlobalGun(gunNo) : null;

  if (gun) {
    state.selectedGun = state.guns.indexOf(gun);
  }
  if (pileCmdMatch) {
    updateFromPileCmd(gun, payload);
  }
  if (logicEventMatch) {
    updateFromLogicEvent(gun, payload);
  }

  pushLog("RX", topic, payload, payload.cmd || payload.event || "MQTT 收到消息");
  sendBackendAction(index, action);
  renderGuns();
}

function sendBackendAction(localGun, action) {
  if (!mqttReady()) {
    pushTimeline("后端未连接", "动作仅在页面预览，未下发 C++ 核");
    return;
  }
  const url = `${brokerUrl()}/api/action?gun=${encodeURIComponent(localGun)}&action=${encodeURIComponent(action)}`;
  fetch(url)
    .then((resp) => resp.json())
    .then((data) => {
      if (!data.ok) {
        pushTimeline("后端动作失败", `${localGun} / ${action}`);
      }
    })
    .catch((err) => {
      pushTimeline("后端动作失败", err.message || String(err));
    });
}

function pushLog(direction, topic, payload, title) {
  const item = {
    time: new Date().toLocaleTimeString(),
    direction,
    topic,
    payload,
    title
  };
  state.logs.unshift(item);
  state.logs = state.logs.slice(0, 80);
  showPreview(direction, topic, payload);
  renderLogs();
  renderSummary();
}

function pushTimeline(title, detail) {
  state.timeline.unshift({
    time: new Date().toLocaleTimeString(),
    title,
    detail
  });
  state.timeline = state.timeline.slice(0, 30);
  renderTimeline();
  renderSummary();
}

function publishPileData(gun, type, data, title) {
  const topic = `${topicPrefix()}/pile/${globalGun(gun)}/data`;
  const payload = pileData(gun, type, data);
  publishToBroker(topic, payload);
  pushLog("TX", topic, payload, title);
}

function publishPileEvent(gun, type, data, title) {
  const topic = `${topicPrefix()}/pile/${globalGun(gun)}/event`;
  const payload = pileEvent(gun, type, data);
  publishToBroker(topic, payload);
  pushLog("TX", topic, payload, title);
}

function publishMeterData(gun, title = "meter data") {
  const topic = `${topicPrefix()}/meter/${globalGun(gun)}/data`;
  const payload = meterData(gun);
  publishToBroker(topic, payload);
  pushLog("TX", topic, payload, title);
}

function publishMeterEvent(gun, event, reason) {
  const topic = `${topicPrefix()}/meter/${globalGun(gun)}/event`;
  const payload = meterEvent(gun, event, reason);
  publishToBroker(topic, payload);
  pushLog("TX", topic, payload, event);
}

function yxData(gun) {
  return {
    workStatus: gun.running ? 1 : 0,
    totalFault: gun.fault ? 1 : 0,
    totalAlarm: 0,
    emergencyStopFault: gun.fault ? 1 : 0,
    vehicleConnectStatus: gun.connected ? 1 : 0,
    vinReq: 0,
    gunSeatStatus: gun.connected ? 1 : 0,
    electronicLockStatus: gun.connected ? 1 : 0,
    dcContactorStatus: gun.running ? 1 : 0,
    otherFault: gun.fault ? 1 : 0
  };
}

function ycData(gun) {
  const measuredVoltageOffset = -1.5;
  const measuredCurrentOffset = -0.5;
  return {
    outputVoltage: Number(gun.voltage),
    outputCurrent: Number(gun.current),
    soc: Math.round(gun.soc),
    batteryMinTemp: 25,
    batteryMaxTemp: 32,
    cellMaxVoltage: 3.65,
    cellMinVoltage: 3.55,
    pileEnvTemp: 28,
    guideVoltage: 12,
    bmsReqVoltage: Number(gun.voltage),
    bmsReqCurrent: Number(gun.current),
    chargeMode: gun.chargeMode,
    bmsMeasuredVoltage: Number(gun.voltage) + measuredVoltageOffset,
    bmsMeasuredCurrent: Math.max(0, Number(gun.current) + measuredCurrentOffset),
    estimatedRemainTime: remainMinutes(gun),
    interfaceTemp1: 28,
    interfaceTemp2: 28,
    interfaceTemp3: 28,
    interfaceTemp4: 28,
    maxVoltageCellNo: 1,
    maxTempPointNo: 1,
    minTempPointNo: 1,
    inletTemp: 25,
    outletTemp: 28,
    envHumidity: 50
  };
}

function startCompleteData(gun, ok = true) {
  return {
    successFlag: ok ? 0 : 1,
    chargeFailReason: ok ? 0 : 14,
    pileBmsVersion: [1, 0, 0],
    bmsPileVersion: [1, 0, 0],
    handshakeResult: ok ? 0 : 1,
    batteryType: 1,
    maxAllowTemp: 60,
    bmsMaxChargeVoltage: Number(gun.voltage),
    cellMaxChargeVoltage: 4200,
    maxAllowChargeCurrent: 250,
    ratedTotalVoltage: 750,
    currentTotalVoltage: Number(gun.voltage) - 10,
    ratedCapacity: 200,
    nominalEnergy: 150,
    soc: Math.round(gun.soc),
    pileMaxOutputVoltage: 1000,
    pileMinOutputVoltage: 200,
    pileMaxOutputCurrent: 250,
    pileMinOutputCurrent: 0,
    vin: gun.vin,
    batteryManufacturer: "TEST",
    batterySerial: [0, 0, 0, 1],
    batteryProdYear: 2026,
    batteryProdMonth: 5,
    batteryProdDay: 15,
    batteryChargeCount: [0, 1, 2],
    batteryPropertyFlag: 0,
    bmsSoftwareVersion: [1, 0, 0, 0, 0, 0, 0, 0]
  };
}

function stopCompleteData(gun) {
  return {
    stopReason: 1,
    stopSuccessFlag: 0,
    bmsStopReason: 0,
    bmsChargeFaultReason: 0,
    bmsStopErrorReason: 0,
    stopSoc: Math.round(gun.soc),
    cellMinVoltage: 3.55,
    cellMaxVoltage: 3.65,
    batteryMinTemp: 25,
    batteryMaxTemp: 32,
    faults: []
  };
}

function vehicleIdData(gun) {
  return {
    vin: gun.vin,
    batteryChargeCount: [0, 1, 2],
    soc: Math.round(gun.soc),
    currentBatteryVoltage: Number(gun.voltage) - 10
  };
}

function tickMeter(gun, intervalSec = 1) {
  if (!gun.running || state.meterPaused || !gun.meterOnline) return;
  const powerKw = Number(gun.voltage) * Number(gun.current) / 1000;
  const deltaKwh = powerKw * intervalSec / 3600;
  const deltaSoc = gun.socRatePerMinute * intervalSec / 60;

  if (gun.mode === "discharge") {
    gun.reverseEnergy = round3(gun.reverseEnergy + deltaKwh);
    gun.soc = clamp(gun.soc - deltaSoc, 0, 100);
  } else {
    gun.totalEnergy = round3(gun.totalEnergy + deltaKwh);
    gun.soc = clamp(gun.soc + deltaSoc, 0, 100);
  }

  publishPileData(gun, "yc", ycData(gun), "yc 周期上送");
  publishMeterData(gun, "meter 周期上送");
}

function actionGun(index, action) {
  const gun = state.guns[index];
  state.selectedGun = index;

  if (action === "plug") {
    gun.connected = true;
    gun.pileState = "PLUGGED";
    publishPileData(gun, "yx", yxData(gun), `${gun.name} 插枪`);
    pushTimeline(`${gun.name} 插枪`, "vehicleConnectStatus=1");
  }

  if (action === "unplug") {
    gun.connected = false;
    gun.running = false;
    gun.pileState = "IDLE";
    publishPileData(gun, "yx", yxData(gun), `${gun.name} 拔枪`);
    pushTimeline(`${gun.name} 拔枪`, "vehicleConnectStatus=0");
  }

  if (action === "startResponse") {
    publishPileEvent(gun, "start_response", {
      confirmFlag: 0,
      startFailReason: 0,
      loadControlSwitch: 1,
      plugAndChargeFlag: gun.mode === "discharge" ? 1 : 1,
      auxPowerVoltage: 12
    }, `${gun.name} 启动应答`);
  }

  if (action === "startComplete") {
    gun.running = true;
    gun.pileState = "OUTPUT";
    publishPileEvent(gun, "start_complete", startCompleteData(gun, true), `${gun.name} 启动完成`);
    publishPileData(gun, "yx", yxData(gun), `${gun.name} 运行 yx`);
    publishPileData(gun, "yc", ycData(gun), `${gun.name} 初始 yc`);
    publishMeterData(gun, `${gun.name} 初始 meter`);
    pushTimeline(`${gun.name} 启动完成`, `VIN=${gun.vin}`);
  }

  if (action === "startFail") {
    gun.running = false;
    gun.pileState = "START_FAIL";
    publishPileEvent(gun, "start_complete", startCompleteData(gun, false), `${gun.name} 启动失败`);
    pushTimeline(`${gun.name} 启动失败`, "successFlag=1");
  }

  if (action === "vin") {
    publishPileEvent(gun, "vehicle_id", vehicleIdData(gun), `${gun.name} 响应 VIN 请求`);
    pushTimeline(`${gun.name} 响应 VIN 请求`, gun.vin);
  }

  if (action === "authAck") {
    publishPileEvent(gun, "vehicle_auth_ack", {
      successFlag: 0,
      failReason: 0
    }, `${gun.name} 鉴权确认`);
  }

  if (action === "stopComplete") {
    gun.running = false;
    gun.pileState = "STOPPED";
    publishPileEvent(gun, "stop_complete", stopCompleteData(gun), `${gun.name} 停机完成`);
    publishPileData(gun, "yx", yxData(gun), `${gun.name} 停机 yx`);
    publishMeterData(gun, `${gun.name} 停机稳定 meter`);
    pushTimeline(`${gun.name} 停机完成`, "等待 meter 稳定收口");
  }

  if (action === "fault") {
    gun.fault = true;
    gun.pileState = "FAULT";
    publishPileData(gun, "yx", yxData(gun), `${gun.name} 故障 yx`);
    publishPileEvent(gun, "deviceErr_on", {
      totalFault: 1,
      otherFault: 1,
      faults: ["emergencyStopFault"]
    }, `${gun.name} 故障触发`);
    pushTimeline(`${gun.name} 故障触发`, "totalFault=1");
  }

  if (action === "recover") {
    gun.fault = false;
    gun.pileState = gun.connected ? "PLUGGED" : "IDLE";
    publishPileData(gun, "yx", yxData(gun), `${gun.name} 故障恢复 yx`);
    publishPileEvent(gun, "deviceErr_off", {
      totalFault: 0,
      otherFault: 0
    }, `${gun.name} 故障恢复`);
    pushTimeline(`${gun.name} 故障恢复`, "totalFault=0");
  }

  renderGuns();
}

function actionPair(action) {
  if (action === "plug") {
    actionGun(0, "plug");
    actionGun(1, "plug");
  }
  if (action === "unplug") {
    actionGun(0, "unplug");
    actionGun(1, "unplug");
  }
  if (action === "vin") {
    actionGun(0, "vin");
    actionGun(1, "vin");
  }
  if (action === "startComplete") {
    actionGun(0, "startComplete");
    actionGun(1, "startComplete");
  }
  if (action === "stopComplete") {
    actionGun(0, "stopComplete");
    actionGun(1, "stopComplete");
  }
  if (action === "fault") {
    actionGun(0, "fault");
    actionGun(1, "fault");
  }
}

function simulateReceivedCmd(cmd) {
  const gun = state.guns[state.selectedGun] || state.guns[0];
  let data = {};

  if (cmd === "start_charge") {
    data = {
      loadControlSwitch: 1,
      plugAndChargeFlag: 1,
      auxPowerVoltage: 12,
      mergeChargeFlag: 0,
      v2g: gun.mode === "discharge" ? 1 : 0
    };
    gun.pileState = "CMD_START";
  }
  if (cmd === "stop_charge") {
    data = { stopReason: 1, tcuStopCode: 0 };
    gun.pileState = "CMD_STOP";
  }
  if (cmd === "vehicle_auth") {
    data = { successFlag: 0, failReason: 0, vin: gun.vin };
  }
  if (cmd === "power_ctrl") {
    data = { maxChargePowerKw: round3(gun.voltage * gun.current / 1000) };
  }

  const topic = `${topicPrefix()}/pile/${globalGun(gun)}/cmd`;
  const payload = rootPayload(gun, "tcu_logic", { cmd, data });
  pushLog("RX", topic, payload, `收到 ${cmd}`);
  pushTimeline(`${gun.name} 收到 ${cmd}`, "Demo 模拟输入");
  renderGuns();
}

function simulateLogicState(logicState) {
  const gun = state.guns[state.selectedGun] || state.guns[0];
  gun.logicState = logicState;
  const topic = `${topicPrefix()}/logic/${globalGun(gun)}/event`;
  const payload = logicEvent(gun, "state_change", {
    state: logicState,
    reason: "manual_demo",
    pileState: gun.pileState,
    vehicleConnectStatus: gun.connected ? 1 : 0,
    workStatus: gun.running ? 1 : 0
  });
  pushLog("RX", topic, payload, `logic 状态 ${logicState}`);
  pushTimeline(`${gun.name} logic 状态`, `${logicState} / 来源 tcu/logic/${globalGun(gun)}/event`);
  renderGuns();
}

function renderGuns() {
  const grid = $("#gunGrid");
  grid.innerHTML = state.guns.map((gun, index) => {
    const classes = ["gun-card"];
    if (gun.connected) classes.push("connected");
    if (gun.fault) classes.push("fault");
    const badgeClass = gun.fault ? "badge danger" : gun.running ? "badge ok" : gun.connected ? "badge" : "badge muted";
    const logicBadgeClass = gun.logicState === "ERROR"
      ? "badge danger"
      : gun.logicState === "CHARGING"
        ? "badge ok"
        : gun.logicState === "UNKNOWN"
          ? "badge muted"
          : "badge";
    const modeLabel = gun.mode === "discharge" ? "放电" : "充电";
    const socWidth = clamp(gun.soc, 0, 100);
    const kw = powerKw(gun).toFixed(1);
    const outputLamp = gun.running ? "lamp on" : "lamp";
    const connectLamp = gun.connected ? "lamp on" : "lamp";
    const faultLamp = gun.fault ? "lamp err" : "lamp";

    return `
      <article class="${classes.join(" ")}" data-gun-card="${index}">
        <div class="gun-title">
          <div>
            <strong>${gun.name}</strong>
            <small>local=${gun.localGun} / global=${globalGun(gun)}</small>
          </div>
          <div>
            <span class="${logicBadgeClass}" title="来源：tcu/logic/${globalGun(gun)}/event">${gun.logicState}</span>
            <small class="state-source">logic/event</small>
            <div class="gun-lamps" aria-label="枪位状态灯">
              <i class="${connectLamp}" title="插枪"></i>
              <i class="${outputLamp}" title="输出"></i>
              <i class="${faultLamp}" title="故障"></i>
            </div>
          </div>
        </div>

        <div class="power-band">
          <div>
            <span>实时输出</span>
            <strong>${kw}<em> kW</em></strong>
          </div>
          <div class="soc-rail">
            <span>SOC ${Math.round(gun.soc)}% / 剩余 ${remainMinutes(gun)} min</span>
            <div class="soc-track"><div class="soc-fill" style="width:${socWidth}%"></div></div>
          </div>
        </div>

        <div class="metric-grid">
          <div class="metric"><span>电压</span><strong>${gun.voltage}V</strong></div>
          <div class="metric"><span>电流</span><strong>${gun.current}A</strong></div>
          <div class="metric"><span>模式</span><strong>${modeLabel}</strong></div>
          <div class="metric"><span>正向电量</span><strong>${gun.totalEnergy.toFixed(3)}</strong></div>
          <div class="metric"><span>反向电量</span><strong>${gun.reverseEnergy.toFixed(3)}</strong></div>
          <div class="metric"><span>pile</span><strong>${gun.pileState}</strong></div>
          <div class="metric"><span>meter</span><strong>${gun.meterOnline ? "ONLINE" : "OFFLINE"}</strong></div>
        </div>

        <div class="gun-config">
          <label>
            <span>VIN</span>
            <input data-field="vin" data-gun="${index}" value="${gun.vin}">
          </label>
          <label>
            <span>电压 V</span>
            <input data-field="voltage" data-gun="${index}" value="${gun.voltage}" inputmode="decimal">
          </label>
          <label>
            <span>电流 A</span>
            <input data-field="current" data-gun="${index}" value="${gun.current}" inputmode="decimal">
          </label>
          <label>
            <span>SOC %</span>
            <input data-field="soc" data-gun="${index}" value="${Math.round(gun.soc)}" inputmode="decimal">
          </label>
          <label>
            <span>模式</span>
            <select data-field="mode" data-gun="${index}">
              <option value="charge" ${gun.mode === "charge" ? "selected" : ""}>充电</option>
              <option value="discharge" ${gun.mode === "discharge" ? "selected" : ""}>放电 V2G</option>
            </select>
          </label>
        </div>

        <div class="gun-actions">
          <button data-gun-action="plug" data-gun="${index}">插枪</button>
          <button data-gun-action="unplug" data-gun="${index}">拔枪</button>
          <button data-gun-action="startResponse" data-gun="${index}">启动应答</button>
          <button data-gun-action="startComplete" data-gun="${index}">启动完成</button>
          <button data-gun-action="startFail" data-gun="${index}">启动失败</button>
          <button data-gun-action="vin" data-gun="${index}">响应 VIN</button>
          <button data-gun-action="authAck" data-gun="${index}">鉴权 ACK</button>
          <button data-gun-action="stopComplete" data-gun="${index}">停机完成</button>
          <button class="danger-btn" data-gun-action="fault" data-gun="${index}">故障触发</button>
          <button data-gun-action="recover" data-gun="${index}">故障恢复</button>
        </div>
      </article>
    `;
  }).join("");
  renderSummary();
}

function renderLogs() {
  $("#logCount").textContent = `${state.logs.length} 条`;
  $("#mqttLog").innerHTML = state.logs.map((log) => `
    <div class="log-item">
      <span>${log.time} <b class="${log.direction === "TX" ? "dir-out" : "dir-in"}">${log.direction}</b></span>
      <code>${log.topic}</code>
      <span>${log.title}</span>
    </div>
  `).join("");
}

function renderTimeline() {
  $("#timeline").innerHTML = state.timeline.map((item) => `
    <div class="timeline-item">
      <strong>${item.title}</strong>
      <span>${item.time} / ${item.detail}</span>
    </div>
  `).join("");
}

function renderSummary() {
  const firstGun = state.guns[0];
  const lastGun = state.guns[state.guns.length - 1];
  const totalPower = state.guns.reduce((sum, gun) => sum + powerKw(gun), 0);
  $("#summaryBroker").textContent = state.connected ? "在线" : "离线";
  $("#summaryGuns").textContent = `${globalGun(firstGun)} / ${globalGun(lastGun)}`;
  $("#summaryPower").textContent = `${totalPower.toFixed(1)} kW`;
  $("#summarySeq").textContent = String(state.seq);
  $("#summaryMeter").textContent = state.meterPaused ? "暂停" : "运行";
}

function bindEvents() {
  $("#connectBtn").addEventListener("click", () => {
    if (state.connected || state.mqttClient) {
      disconnectBroker();
      return;
    }
    connectBroker();
  });

  $("#tickOnceBtn").addEventListener("click", () => {
    state.guns.forEach((gun, index) => {
      tickMeter(gun, 1);
      sendBackendAction(index, "tick");
    });
    renderGuns();
  });

  $("#clearLogBtn").addEventListener("click", () => {
    state.logs = [];
    state.timeline = [];
    renderLogs();
    renderTimeline();
  });

  document.addEventListener("click", (event) => {
    const gunAction = event.target.closest("[data-gun-action]");
    if (gunAction) {
      actionGun(Number(gunAction.dataset.gun), gunAction.dataset.gunAction);
      return;
    }

    const pairAction = event.target.closest("[data-pair-action]");
    if (pairAction) {
      actionPair(pairAction.dataset.pairAction);
      return;
    }

    const cmd = event.target.closest("[data-cmd]");
    if (cmd) {
      simulateReceivedCmd(cmd.dataset.cmd);
      return;
    }

    const logicState = event.target.closest("[data-logic-state]");
    if (logicState) {
      simulateLogicState(logicState.dataset.logicState);
      return;
    }

    const meter = event.target.closest("[data-meter]");
    if (meter) {
      const gun = state.guns[state.selectedGun] || state.guns[0];
      const action = meter.dataset.meter;
      if (action === "online") {
        gun.meterOnline = true;
        publishMeterEvent(gun, "meter_online", "manual_recover");
        sendBackendAction(state.selectedGun, "meterOnline");
      }
      if (action === "offline") {
        gun.meterOnline = false;
        publishMeterEvent(gun, "meter_offline", "manual_inject");
        sendBackendAction(state.selectedGun, "meterOffline");
      }
      if (action === "pause") {
        state.meterPaused = true;
        pushTimeline("自动计量暂停", "meter data 不再自动递增");
      }
      if (action === "resume") {
        state.meterPaused = false;
        pushTimeline("自动计量恢复", "meter data 恢复递增");
      }
      renderGuns();
    }
  });

  document.addEventListener("input", (event) => {
    const field = event.target.closest("[data-field]");
    if (!field) return;
    const gun = state.guns[Number(field.dataset.gun)];
    const key = field.dataset.field;
    const value = field.value;
    if (key === "vin" || key === "mode") {
      gun[key] = value;
    } else {
      gun[key] = Number(value) || 0;
    }
  });

  document.addEventListener("change", (event) => {
    const field = event.target.closest("[data-field]");
    if (!field) return;
    renderGuns();
  });

  $("#biasNo").addEventListener("input", renderGuns);
}

function boot() {
  renderGuns();
  renderLogs();
  renderTimeline();
  bindEvents();
  showPreview("等待动作", "尚未生成报文", {});
  pushTimeline("virtualPlug 已加载", "连接本地 C++ 后端后由后端收发 MQTT");
}

boot();
