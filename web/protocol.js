export const USB_VENDOR_ID = 0xcafe;
export const USB_PRODUCT_ID = 0x4008;
export const VENDOR_USAGE_PAGE = 0xff00;
export const VENDOR_USAGE = 0x01;
export const FRAME_SIZE = 64;
export const PAYLOAD_MAX = 56;
export const SCHEMA_VERSION = 1;
export const MAX_ACTIONS = 32;

export const Command = Object.freeze({
  GET_STATUS: 0x01,
  GET_CONFIG_INFO: 0x02,
  GET_CONFIG_CHUNK: 0x03,
  STAGE_BEGIN: 0x10,
  STAGE_CHUNK: 0x11,
  STAGE_VALIDATE: 0x12,
  RUN_ONCE: 0x13,
  APPLY_TEMP: 0x14,
  APPLY_SAVE: 0x15,
  RESTORE_DEFAULT: 0x16,
  REBOOT_APPLICATION: 0x17,
  REBOOT_BOOTSEL: 0x18,
});

export const StatusName = Object.freeze([
  "成功", "魔数错误", "协议版本错误", "长度错误", "未知命令", "事务错误", "偏移错误", "CRC 错误", "配置错误", "设备忙", "Flash 错误", "数据不完整",
]);

const actionTypes = new Set(["delay", "move", "mouseClick", "keyboardClick"]);

function integerInRange(value, minimum, maximum, label) {
  if (!Number.isInteger(value) || value < minimum || value > maximum) {
    throw new Error(`${label} 必须是 ${minimum}–${maximum} 的整数`);
  }
}

export function validateConfig(config) {
  if (typeof config !== "object" || config === null) throw new Error("配置必须是对象");
  if (typeof config.enabled !== "boolean") throw new Error("启用标志必须是布尔值");
  integerInRange(config.repeatIntervalMs, 1, 86400000, "循环周期");
  if (!Array.isArray(config.actions) || config.actions.length > MAX_ACTIONS) throw new Error(`动作数量必须是 0–${MAX_ACTIONS}`);
  config.actions.forEach((action, index) => {
    const prefix = `动作 ${index + 1}`;
    if (!actionTypes.has(action.type)) throw new Error(`${prefix} 类型无效`);
    if (action.type === "delay") integerInRange(action.durationMs, 1, 60000, `${prefix} 延时`);
    if (action.type === "move") {
      integerInRange(action.x, -127, 127, `${prefix} X`);
      integerInRange(action.y, -127, 127, `${prefix} Y`);
      integerInRange(action.wheel, -127, 127, `${prefix} 滚轮`);
      integerInRange(action.pan, -127, 127, `${prefix} 横向滚轮`);
      integerInRange(action.durationMs, 0, 60000, `${prefix} 移动时长`);
    }
    if (action.type === "mouseClick") {
      integerInRange(action.buttons, 1, 31, `${prefix} 按钮掩码`);
      integerInRange(action.holdMs, 10, 1000, `${prefix} 按住时间`);
    }
    if (action.type === "keyboardClick") {
      integerInRange(action.modifiers, 0, 255, `${prefix} 修饰键`);
      integerInRange(action.usage, 1, 255, `${prefix} HID Usage`);
      integerInRange(action.holdMs, 10, 1000, `${prefix} 按住时间`);
    }
  });
  return config;
}

export function defaultConfig() {
  return {
    enabled: true,
    repeatIntervalMs: 55000,
    actions: [
      { type: "move", x: 100, y: 0, wheel: 0, pan: 0, durationMs: 0 },
      { type: "delay", durationMs: 200 },
      { type: "move", x: -100, y: 0, wheel: 0, pan: 0, durationMs: 0 },
    ],
  };
}

export function encodeConfig(config) {
  validateConfig(config);
  const bytes = new Uint8Array(12 + config.actions.length * 8);
  const view = new DataView(bytes.buffer);
  bytes[0] = SCHEMA_VERSION;
  bytes[1] = config.enabled ? 1 : 0;
  bytes[2] = config.actions.length;
  view.setUint32(4, config.repeatIntervalMs, true);
  config.actions.forEach((action, index) => {
    const offset = 12 + index * 8;
    if (action.type === "delay") {
      bytes[offset] = 1;
      view.setUint32(offset + 4, action.durationMs, true);
    } else if (action.type === "move") {
      bytes[offset] = 2;
      view.setInt8(offset + 1, action.x);
      view.setInt8(offset + 2, action.y);
      view.setInt8(offset + 3, action.wheel);
      view.setInt8(offset + 4, action.pan);
      view.setUint16(offset + 5, action.durationMs, true);
    } else if (action.type === "mouseClick") {
      bytes[offset] = 3;
      bytes[offset + 1] = action.buttons;
      view.setUint16(offset + 2, action.holdMs, true);
    } else {
      bytes[offset] = 4;
      bytes[offset + 1] = action.modifiers;
      bytes[offset + 2] = action.usage;
      view.setUint16(offset + 4, action.holdMs, true);
    }
  });
  return bytes;
}

export function decodeConfig(data) {
  const bytes = data instanceof Uint8Array ? data : new Uint8Array(data);
  if (bytes.length < 12 || bytes[0] !== SCHEMA_VERSION || (bytes[1] & 0xfe) !== 0 || bytes[3] !== 0 || bytes.slice(8, 12).some(Boolean)) {
    throw new Error("配置头无效");
  }
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const count = bytes[2];
  if (count > MAX_ACTIONS || bytes.length !== 12 + count * 8) throw new Error("配置长度无效");
  const config = { enabled: Boolean(bytes[1] & 1), repeatIntervalMs: view.getUint32(4, true), actions: [] };
  for (let index = 0; index < count; index += 1) {
    const offset = 12 + index * 8;
    const wire = bytes.slice(offset, offset + 8);
    if (wire[0] === 1) {
      if (wire[1] || wire[2] || wire[3]) throw new Error("延时动作保留字段非零");
      config.actions.push({ type: "delay", durationMs: view.getUint32(offset + 4, true) });
    } else if (wire[0] === 2) {
      if (wire[7]) throw new Error("移动动作保留字段非零");
      config.actions.push({ type: "move", x: view.getInt8(offset + 1), y: view.getInt8(offset + 2), wheel: view.getInt8(offset + 3), pan: view.getInt8(offset + 4), durationMs: view.getUint16(offset + 5, true) });
    } else if (wire[0] === 3) {
      if (wire[4] || wire[5] || wire[6] || wire[7]) throw new Error("点击动作保留字段非零");
      config.actions.push({ type: "mouseClick", buttons: wire[1], holdMs: view.getUint16(offset + 2, true) });
    } else if (wire[0] === 4) {
      if (wire[3] || wire[6] || wire[7]) throw new Error("键盘动作保留字段非零");
      config.actions.push({ type: "keyboardClick", modifiers: wire[1], usage: wire[2], holdMs: view.getUint16(offset + 4, true) });
    } else {
      throw new Error("动作类型无效");
    }
  }
  return validateConfig(config);
}

export function crc32(data) {
  const bytes = data instanceof Uint8Array ? data : new Uint8Array(data);
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc = (crc ^ byte) >>> 0;
    for (let bit = 0; bit < 8; bit += 1) crc = ((crc >>> 1) ^ ((-(crc & 1)) & 0xedb88320)) >>> 0;
  }
  return (~crc) >>> 0;
}

export function createFrame(command, transaction, payload = new Uint8Array()) {
  if (payload.length > PAYLOAD_MAX) throw new Error("协议载荷过长");
  const frame = new Uint8Array(FRAME_SIZE);
  const view = new DataView(frame.buffer);
  frame[0] = 0x48;
  frame[1] = 0x50;
  frame[2] = 1;
  frame[3] = command;
  view.setUint16(4, transaction, true);
  frame[6] = payload.length;
  frame.set(payload, 8);
  return frame;
}

export function parseResponse(data, expectedCommand, expectedTransaction) {
  const frame = data instanceof Uint8Array ? data : new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
  if (frame.length !== FRAME_SIZE || frame[0] !== 0x48 || frame[1] !== 0x50 || frame[2] !== 1) throw new Error("设备响应帧无效");
  const view = new DataView(frame.buffer, frame.byteOffset, frame.byteLength);
  const transaction = view.getUint16(4, true);
  if (frame[3] !== (expectedCommand | 0x80) || transaction !== expectedTransaction) throw new Error("设备响应与请求不匹配");
  if (frame[6] > PAYLOAD_MAX) throw new Error("设备响应长度无效");
  if (frame[7] !== 0) throw new Error(StatusName[frame[7]] ?? `设备错误 ${frame[7]}`);
  return frame.slice(8, 8 + frame[6]);
}

export class HidPilotClient {
  constructor(device) {
    this.device = device;
    this.transaction = 0;
    this.pending = null;
    this.onInput = this.onInput.bind(this);
  }

  async open() {
    if (!this.device.opened) await this.device.open();
    this.device.addEventListener("inputreport", this.onInput);
  }

  close() {
    this.device.removeEventListener("inputreport", this.onInput);
    if (this.pending) this.pending.reject(new Error("设备已断开"));
    this.pending = null;
  }

  onInput(event) {
    if (!this.pending || event.reportId !== 0) return;
    try {
      const payload = parseResponse(event.data, this.pending.command, this.pending.transaction);
      this.pending.resolve(payload);
    } catch (error) {
      this.pending.reject(error);
    } finally {
      clearTimeout(this.pending?.timer);
      this.pending = null;
    }
  }

  nextTransaction() {
    this.transaction = (this.transaction + 1) & 0xffff;
    if (this.transaction === 0) this.transaction = 1;
    return this.transaction;
  }

  request(command, payload = new Uint8Array(), transaction = this.nextTransaction()) {
    if (this.pending) return Promise.reject(new Error("已有请求正在等待响应"));
    const frame = createFrame(command, transaction, payload);
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending = null;
        reject(new Error("等待设备响应超时"));
      }, 2000);
      this.pending = { command, transaction, resolve, reject, timer };
      this.device.sendReport(0, frame).catch((error) => {
        clearTimeout(timer);
        this.pending = null;
        reject(error);
      });
    });
  }

  async status() {
    const payload = await this.request(Command.GET_STATUS);
    const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
    return {
      firmware: `${payload[0]}.${payload[1]}.${payload[2]}`,
      schema: payload[3],
      flags: payload[4],
      actionIndex: payload[5],
      actionCount: payload[6],
      activeSlot: payload[7],
      repeatIntervalMs: view.getUint32(8, true),
      completedRuns: view.getUint32(12, true),
      errors: view.getUint32(16, true),
      generation: view.getUint32(20, true),
      crc: view.getUint32(24, true),
      configLength: view.getUint16(28, true),
    };
  }

  async readConfig() {
    const info = await this.request(Command.GET_CONFIG_INFO);
    const infoView = new DataView(info.buffer, info.byteOffset, info.byteLength);
    const length = infoView.getUint16(0, true);
    const expectedCrc = infoView.getUint32(2, true);
    const bytes = new Uint8Array(length);
    for (let offset = 0; offset < length;) {
      const count = Math.min(54, length - offset);
      const request = new Uint8Array(3);
      const view = new DataView(request.buffer);
      view.setUint16(0, offset, true);
      request[2] = count;
      const chunk = await this.request(Command.GET_CONFIG_CHUNK, request);
      const returnedOffset = new DataView(chunk.buffer, chunk.byteOffset, chunk.byteLength).getUint16(0, true);
      if (returnedOffset !== offset || chunk.length !== count + 2) throw new Error("配置分块响应无效");
      bytes.set(chunk.slice(2), offset);
      offset += count;
    }
    if (crc32(bytes) !== expectedCrc) throw new Error("读取配置的 CRC 不匹配");
    return decodeConfig(bytes);
  }

  async stage(config) {
    const data = encodeConfig(config);
    const transaction = this.nextTransaction();
    const begin = new Uint8Array(6);
    const view = new DataView(begin.buffer);
    view.setUint16(0, data.length, true);
    view.setUint32(2, crc32(data), true);
    await this.request(Command.STAGE_BEGIN, begin, transaction);
    for (let offset = 0; offset < data.length;) {
      const count = Math.min(54, data.length - offset);
      const chunk = new Uint8Array(count + 2);
      new DataView(chunk.buffer).setUint16(0, offset, true);
      chunk.set(data.slice(offset, offset + count), 2);
      await this.request(Command.STAGE_CHUNK, chunk, transaction);
      offset += count;
    }
    await this.request(Command.STAGE_VALIDATE, new Uint8Array(), transaction);
    return transaction;
  }

  async stageAndRun(config, command) {
    const transaction = await this.stage(config);
    await this.request(command, new Uint8Array(), transaction);
  }
}
