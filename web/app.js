import { Command, HidPilotClient, USB_PRODUCT_ID, USB_VENDOR_ID, VENDOR_USAGE, VENDOR_USAGE_PAGE, defaultConfig, hasAccelerationSensitiveReturn, validateConfig } from "./protocol.js";
import { millisecondsToSeconds, secondsToMilliseconds } from "./units.js";

const elements = Object.fromEntries([...document.querySelectorAll("[id]")].map((element) => [element.id, element]));
let client = null;
let currentDevice = null;

const actionDefaults = {
  delay: () => ({ type: "delay", durationMs: 200 }),
  move: () => ({ type: "move", x: 0, y: 0, wheel: 0, pan: 0, durationMs: 0 }),
  mouseClick: () => ({ type: "mouseClick", buttons: 1, holdMs: 50 }),
  keyboardClick: () => ({ type: "keyboardClick", modifiers: 0, usage: 4, holdMs: 50 }),
};

function setNotice(message, kind = "") {
  elements.notice.textContent = message;
  elements.notice.className = `notice ${kind}`.trim();
}

function numberField(label, name, value, minimum, maximum) {
  return `<label class="field">${label}<input data-field="${name}" type="number" min="${minimum}" max="${maximum}" step="1" value="${value}"></label>`;
}

function secondsField(label, name, valueMs, minimumMs, maximumMs) {
  return `<label class="field">${label}<input data-field="${name}" data-unit="seconds" data-minimum-ms="${minimumMs}" data-maximum-ms="${maximumMs}" data-label="${label}" type="number" min="${millisecondsToSeconds(minimumMs)}" max="${millisecondsToSeconds(maximumMs)}" step="0.001" value="${millisecondsToSeconds(valueMs)}"></label>`;
}

function renderAction(action, index) {
  const row = document.createElement("div");
  row.className = "action";
  row.dataset.type = action.type;
  const labels = { delay: "延时", move: "相对鼠标", mouseClick: "鼠标点击", keyboardClick: "键盘组合键" };
  let fields = "";
  if (action.type === "delay") fields = secondsField("时长 s", "durationMs", action.durationMs, 1, 60000);
  if (action.type === "move") fields = [numberField("X 总位移", "x", action.x, -500, 500), numberField("Y 总位移", "y", action.y, -500, 500), numberField("滚轮", "wheel", action.wheel, -127, 127), numberField("横向", "pan", action.pan, -127, 127), secondsField("移动时长 s", "durationMs", action.durationMs, 0, 60000)].join("");
  if (action.type === "mouseClick") fields = [numberField("按钮掩码", "buttons", action.buttons, 1, 31), secondsField("按住 s", "holdMs", action.holdMs, 10, 1000)].join("");
  if (action.type === "keyboardClick") fields = [numberField("修饰键", "modifiers", action.modifiers, 0, 255), numberField("HID Usage", "usage", action.usage, 1, 255), secondsField("按住 s", "holdMs", action.holdMs, 10, 1000)].join("");
  row.innerHTML = `<div class="action-title"><strong>${labels[action.type]}</strong><span>动作 ${index + 1}</span></div><div class="action-fields">${fields}</div><div class="action-buttons"><button data-action="up" title="上移">↑</button><button data-action="down" title="下移">↓</button><button data-action="delete" title="删除">删除</button></div>`;
  return row;
}

function setConfig(config) {
  elements.enabled.checked = config.enabled;
  elements.interval.value = millisecondsToSeconds(config.repeatIntervalMs);
  elements.actions.replaceChildren(...config.actions.map(renderAction));
  if (config.actions.length === 0) elements.actions.innerHTML = '<div class="empty">当前没有动作；启用时每个周期只更新运行计数。</div>';
}

function collectConfig() {
  const actions = [...elements.actions.querySelectorAll(".action")].map((row) => {
    const action = { type: row.dataset.type };
    row.querySelectorAll("[data-field]").forEach((input) => {
      action[input.dataset.field] = input.dataset.unit === "seconds"
        ? secondsToMilliseconds(input.value, Number(input.dataset.minimumMs), Number(input.dataset.maximumMs), input.dataset.label)
        : Number(input.value);
    });
    return action;
  });
  const repeatIntervalMs = secondsToMilliseconds(elements.interval.value, 1, 86400000, "循环周期");
  return validateConfig({ enabled: elements.enabled.checked, repeatIntervalMs, actions });
}

function connected() {
  if (!client) throw new Error("请先连接 HIDPilot");
  return client;
}

function risky(config) {
  return config.actions.some((action) => action.type === "mouseClick" || action.type === "keyboardClick");
}

async function safetyCountdown(config) {
  if (hasAccelerationSensitiveReturn(config) && !window.confirm("检测到等量反向移动使用了不同的移动时长。操作系统指针加速会导致屏幕往返距离不同；如需回到原位，请把正反动作设为相同移动时长。仍要继续？")) throw new Error("已取消操作");
  if (!risky(config)) return;
  if (!window.confirm("此配置包含点击或键盘动作，可能触发当前应用中的操作。确认继续？")) throw new Error("已取消操作");
  for (let seconds = 3; seconds > 0; seconds -= 1) {
    setNotice(`安全倒计时 ${seconds} 秒：请切换到允许接收输入的目标窗口。`);
    await new Promise((resolve) => setTimeout(resolve, 1000));
  }
}

async function refresh() {
  const deviceClient = connected();
  const config = await deviceClient.readConfig();
  const status = await deviceClient.status();
  setConfig(config);
  elements.version.textContent = `${status.firmware} / v${status.schema}`;
  elements.runtime.textContent = `${status.flags & 1 ? "启用" : "暂停"}${status.flags & 4 ? " · 挂起" : ""}${status.flags & 8 ? " · 执行中" : ""}`;
  elements.runs.textContent = `${status.completedRuns}（错误 ${status.errors}）`;
  setNotice(`配置已读取：${status.actionCount} 个动作，周期 ${millisecondsToSeconds(status.repeatIntervalMs)} s，Flash 代际 ${status.generation}。`, "ok");
}

async function connect() {
  if (!("hid" in navigator)) throw new Error("此浏览器不支持 WebHID；请使用桌面版 Chrome 或 Edge");
  const devices = await navigator.hid.requestDevice({ filters: [{ vendorId: USB_VENDOR_ID, productId: USB_PRODUCT_ID, usagePage: VENDOR_USAGE_PAGE, usage: VENDOR_USAGE }] });
  if (!devices.length) throw new Error("未选择设备");
  currentDevice = devices[0];
  client = new HidPilotClient(currentDevice);
  await client.open();
  elements.connection.textContent = `${currentDevice.productName ?? "HIDPilot"} 已连接`;
  elements.connect.textContent = "重新连接";
  await refresh();
}

async function perform(label, task) {
  try {
    [...document.querySelectorAll("button")].forEach((button) => { button.disabled = true; });
    setNotice(`${label}…`);
    await task();
    setNotice(`${label}完成。`, "ok");
  } catch (error) {
    setNotice(error.message, "error");
  } finally {
    [...document.querySelectorAll("button")].forEach((button) => { button.disabled = false; });
  }
}

elements.actions.addEventListener("click", (event) => {
  const button = event.target.closest("button[data-action]");
  if (!button) return;
  const row = button.closest(".action");
  if (button.dataset.action === "delete") row.remove();
  if (button.dataset.action === "up" && row.previousElementSibling) row.parentNode.insertBefore(row, row.previousElementSibling);
  if (button.dataset.action === "down" && row.nextElementSibling) row.parentNode.insertBefore(row.nextElementSibling, row);
  const config = collectConfig();
  setConfig(config);
});

elements.add.addEventListener("click", () => {
  try {
    const config = collectConfig();
    if (config.actions.length >= 32) throw new Error("最多只能配置 32 个动作");
    config.actions.push(actionDefaults[elements["new-action"].value]());
    setConfig(config);
  } catch (error) {
    setNotice(error.message, "error");
  }
});

elements.connect.addEventListener("click", () => perform("连接设备", connect));
elements.refresh.addEventListener("click", () => perform("读取配置", refresh));
elements.run.addEventListener("click", () => perform("单次试运行", async () => { const config = collectConfig(); await safetyCountdown(config); await connected().stageAndRun(config, Command.RUN_ONCE); }));
elements.apply.addEventListener("click", () => perform("临时应用", async () => { const config = collectConfig(); await safetyCountdown(config); await connected().stageAndRun(config, Command.APPLY_TEMP); await refresh(); }));
elements.save.addEventListener("click", () => perform("保存到 Flash", async () => { const config = collectConfig(); await safetyCountdown(config); await connected().stageAndRun(config, Command.APPLY_SAVE); await refresh(); }));
elements.pause.addEventListener("click", () => perform("持久化暂停", async () => { const config = collectConfig(); config.enabled = false; setConfig(config); await connected().stageAndRun(config, Command.APPLY_SAVE); await refresh(); }));
elements.restore.addEventListener("click", () => perform("恢复并保存默认配置", async () => { if (!window.confirm("确认恢复并保存默认配置？")) throw new Error("已取消操作"); await connected().request(Command.RESTORE_DEFAULT); await refresh(); }));
elements.reboot.addEventListener("click", () => perform("重启应用", async () => { await connected().request(Command.REBOOT_APPLICATION); setNotice("重启命令已发送，设备将短暂断开。", "ok"); }));
elements.bootsel.addEventListener("click", () => perform("进入 BOOTSEL", async () => { if (!window.confirm("确认让设备进入 BOOTSEL 烧录模式？")) throw new Error("已取消操作"); await connected().request(Command.REBOOT_BOOTSEL); setNotice("BOOTSEL 命令已发送。", "ok"); }));

if ("hid" in navigator) {
  navigator.hid.addEventListener("disconnect", (event) => {
    if (event.device !== currentDevice) return;
    client?.close();
    client = null;
    currentDevice = null;
    elements.connection.textContent = "已断开";
    setNotice("设备已断开；重新连接后请再次读取配置。", "error");
  });
} else {
  elements.connect.disabled = true;
  setNotice("当前浏览器不支持 WebHID。请使用桌面版 Chrome 或 Edge；Safari 与 Firefox 不支持。", "error");
}

setConfig(defaultConfig());
