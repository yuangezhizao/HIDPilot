export function millisecondsToSeconds(milliseconds) {
  if (!Number.isInteger(milliseconds)) throw new Error("毫秒值必须是整数");
  return milliseconds / 1000;
}

export function secondsToMilliseconds(value, minimumMs, maximumMs, label) {
  const seconds = Number(value);
  if (!Number.isFinite(seconds)) throw new Error(`${label}必须是数字`);

  const scaled = seconds * 1000;
  const milliseconds = Math.round(scaled);
  const tolerance = Number.EPSILON * Math.max(1, Math.abs(scaled)) * 8;
  if (Math.abs(scaled - milliseconds) > tolerance) throw new Error(`${label}最多支持 3 位小数`);
  if (milliseconds < minimumMs || milliseconds > maximumMs) {
    throw new Error(`${label}必须是 ${millisecondsToSeconds(minimumMs)}–${millisecondsToSeconds(maximumMs)} 秒`);
  }
  return milliseconds;
}
