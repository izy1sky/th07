// ToyBridge: B站 Toy JS SDK 桥接层（供 Emscripten 游戏调用）。
// 非 Toy 环境下 window.toy 不存在，所有能力自动降级。
(function () {
  "use strict";

  if (typeof window.ToyBridge !== "undefined") return;

  const hasToy = () => typeof window.toy !== "undefined";
  const support = async (name) => hasToy() && (await window.toy.isSupport(name));
  const warn = (msg, e) => console.warn("[ToyBridge] " + msg, e);

  // 最近一局结算，等待用户点击“上传成绩”时提交
  let lastRun = null;

  async function getUser() {
    if (!(await support("getUserProfile"))) return null;
    try {
      const { avatar, nickname, toyOpenId } = await window.toy.getUserProfile();
      // toyOpenId 只用于当前 Toy 内关联，禁止写入日志/埋点
      return { avatar, nickname, toyOpenId };
    } catch (e) {
      warn("getUser failed", e);
      return null;
    }
  }

  async function appendHistory(entry) {
    if (!(await support("getCloudStorage"))) return false;
    try {
      const all = await window.toy.getCloudStorage();
      let list = [];
      try {
        list = JSON.parse(all["history"] || "[]");
      } catch (e) {
        list = [];
      }
      list.unshift(entry);
      list = list.slice(0, 20);
      await window.toy.setCloudStorage({ history: JSON.stringify(list) });
      return true;
    } catch (e) {
      warn("appendHistory failed", e);
      return false;
    }
  }

  async function getHistory() {
    if (!(await support("getCloudStorage"))) return [];
    try {
      const all = await window.toy.getCloudStorage();
      return JSON.parse(all["history"] || "[]");
    } catch (e) {
      return [];
    }
  }

  async function submitScore(score, board = 1) {
    if (!(await support("submitScore"))) return null;
    try {
      const clamped = Math.max(-16777216, Math.min(16777215, Math.trunc(score)));
      return await window.toy.submitScore({ board, score: clamped });
    } catch (e) {
      warn("submitScore failed", e);
      return null;
    }
  }

  async function getRank(board = 1, period = "all", limit = 50) {
    if (!(await support("getRankList"))) return [];
    try {
      return await window.toy.getRankList({ board, period, limit });
    } catch (e) {
      return [];
    }
  }

  async function getMyRank(board = 1, period = "all") {
    if (!(await support("getMyRank"))) return null;
    try {
      return await window.toy.getMyRank({ board, period });
    } catch (e) {
      return null;
    }
  }

  // App 内保存相册，Web 端触发下载
  async function shareImage(dataUrl) {
    if (await support("saveImageToAlbum")) {
      try {
        await window.toy.saveImageToAlbum({ base64Data: dataUrl.split(",")[1] });
        return "album";
      } catch (e) {
        warn("saveImageToAlbum failed, falling back to download", e);
      }
    }
    const a = document.createElement("a");
    a.href = dataUrl;
    a.download = "th07-score.png";
    a.click();
    return "download";
  }

  // C++ 结算钩子：记录最近一局，并尽力写一条云历史（≤1KB）
  async function onRunEnded(type, score, stage, difficulty, character, shotType) {
    const now = new Date();
    const pad = (n) => String(n).padStart(2, "0");
    lastRun = {
      d: `${now.getFullYear()}-${pad(now.getMonth() + 1)}-${pad(now.getDate())}`,
      t: type,
      s: score,
      st: stage,
      df: difficulty,
      c: character,
      stp: shotType,
    };
    // 云存储不需要手势，但需要登录；失败静默即可
    await appendHistory(lastRun);
  }

  // 供结算页按钮调用：提交最近一局成绩（需用户手势触发首次确认）
  async function submitLastScore(board = 1, scale = 1000) {
    if (!lastRun) return null;
    // 榜 1 用 score/1000（有损，页面需注明规则）
    return await submitScore(lastRun.s / scale, board);
  }

  window.ToyBridge = {
    getUser,
    appendHistory,
    getHistory,
    submitScore,
    getRank,
    getMyRank,
    shareImage,
    onRunEnded,
    submitLastScore,
    get hasToy() {
      return hasToy();
    },
  };
})();
