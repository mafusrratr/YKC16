### 1 平台在线事件

Topic：`tcu/plat/{gun}/event`

示例：

```json
{
  "ts": 1774430000000,
  "seq": 12,
  "source": "tcu_comm",
  "gun": 0,
  "event": "platform_online",
  "data": {
    "reason": "login_ok"
  }
}



### 2 等待tcu_logic event 发布的消息

Topic: tcu/logic/0/event
QoS: 0
{"ts":1778148945384,"seq":15,"source":"tcu_logic","gun":0,"event":"plug_and_charge_vehicle_id_confirmed","data":{"vin":"LJ21BABB8L1001955"}}

意味着读取完成vin读取完成，等待鉴权结果




### 2 等待tcu_logic event 发布的消息

Topic: tcu/logic/0/event
QoS: 0
{"ts":1778148945384,"seq":15,"source":"tcu_logic","gun":0,"event":"plug_and_charge_vehicle_id_confirmed","data":{"vin":"LJ21BABB8L1001955"}}

意味着读取完成vin读取完成，等待鉴权结果






### 3 发布VIN启动的消息
即插即充业务参数示例（平台 -> logic）：
Topic：`tcu/plat/{gun}/cmd`
```json
{
  "ts": 1736150003000,
  "seq": 11,
  "source": "platform",
  "gun": 0,
  "cmd": "start_charge",
  "data": {
    "vin": "LJ21BABB8L1001955",
    "chargeUserNo": "U10001",
    "orderNo": "P202602120001",
    "preTradeNo": "P202602120001",
    "tradeNo": "P202602120001",
    "plugAndChargeFlag": 2,
    "mergeChargeFlag": 0,
    "chargeMode": 1,
    "prechargeAmount": 100.0,
    "feeModelNo": 1,
    "feeModelId": "MODEL001",
    "timeNum": 12,
    "timeSeg": ["0000", "1200", "1430", "1535", "1538", "1540", "1550", "1600", "1730", "1830", "1930", "2130"],
    "chargeFee": [7, 0.90, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2],
    "serviceFee": [1, 0.15, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2]
  }
}