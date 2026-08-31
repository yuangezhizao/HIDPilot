import test from "node:test";
import assert from "node:assert/strict";
import { Command, createFrame, crc32, decodeConfig, defaultConfig, encodeConfig, hasAccelerationSensitiveReturn, parseResponse, validateConfig } from "./protocol.js";

test("CRC-32/ISO-HDLC 标准向量", () => {
  assert.equal(crc32(new TextEncoder().encode("123456789")), 0xcbf43926);
});

test("默认配置 wire round-trip", () => {
  const config = defaultConfig();
  const encoded = encodeConfig(config);
  assert.equal(encoded.length, 36);
  assert.deepEqual(decodeConfig(encoded), config);
});

test("完整动作集合 round-trip", () => {
  const config = {
    enabled: false,
    repeatIntervalMs: 123456,
    actions: [
      { type: "delay", durationMs: 60000 },
      { type: "move", x: -500, y: 500, wheel: -1, pan: 1, durationMs: 60000 },
      { type: "mouseClick", buttons: 31, holdMs: 1000 },
      { type: "keyboardClick", modifiers: 255, usage: 4, holdMs: 10 },
    ],
  };
  assert.deepEqual(decodeConfig(encodeConfig(config)), config);
});

test("表单边界校验", () => {
  assert.throws(() => validateConfig({ enabled: true, repeatIntervalMs: 0, actions: [] }), /循环周期/);
  assert.throws(() => validateConfig({ enabled: true, repeatIntervalMs: 1, actions: [{ type: "move", x: 501, y: 0, wheel: 0, pan: 0, durationMs: 0 }] }), /X/);
  assert.throws(() => validateConfig({ enabled: true, repeatIntervalMs: 1, actions: [{ type: "move", x: 0, y: 0, wheel: 0, pan: 0, durationMs: 60001 }] }), /移动时长/);
  assert.throws(() => validateConfig({ enabled: true, repeatIntervalMs: 1, actions: Array.from({ length: 33 }, () => ({ type: "delay", durationMs: 1 })) }), /动作数量/);
  assert.doesNotThrow(() => validateConfig({ enabled: false, repeatIntervalMs: 1, actions: [] }));
});

test("识别受指针加速影响的异速往返动作", () => {
  const base = { enabled: true, repeatIntervalMs: 1000 };
  assert.equal(hasAccelerationSensitiveReturn({ ...base, actions: [
    { type: "move", x: 127, y: 0, wheel: 0, pan: 0, durationMs: 500 },
    { type: "delay", durationMs: 500 },
    { type: "move", x: -127, y: 0, wheel: 0, pan: 0, durationMs: 0 },
  ] }), true);
  assert.equal(hasAccelerationSensitiveReturn({ ...base, actions: [
    { type: "move", x: 500, y: 0, wheel: 0, pan: 0, durationMs: 500 },
    { type: "move", x: -500, y: 0, wheel: 0, pan: 0, durationMs: 500 },
  ] }), false);
});

test("协议帧编码与响应事务校验", () => {
  const request = createFrame(Command.GET_STATUS, 0x1234);
  assert.equal(request.length, 64);
  assert.deepEqual([...request.slice(0, 8)], [0x48, 0x50, 1, 1, 0x34, 0x12, 0, 0]);
  const response = createFrame(Command.GET_STATUS | 0x80, 0x1234, Uint8Array.of(7));
  assert.deepEqual([...parseResponse(response, Command.GET_STATUS, 0x1234)], [7]);
  assert.throws(() => parseResponse(response, Command.GET_STATUS, 0x4321), /不匹配/);
});
