window.safeFetch = async (url, options, actionName, type = 'text') => {
    const res = await fetch(url, options).catch(() => null);
    if (!res || !res.ok) {showinbox(res?.status ? `${actionName}失败 (状态码: ${res.status})` : '设备连接已断开。');return null;}
    try {return type === 'json' ? await res.json() : await res.text();} catch (e) {
        showinbox(`${actionName}数据解析异常，请检查接口返回格式。`);return null;}
};
// AD = Hash(Hash(rd0 + rd1) + RD), 逻辑参考自service.js，生成goform接口校验参数AD
window.getAD = async () => {
    const data = await safeFetch('/goform/goform_get_cmd_process?cmd=RD', {}, '获取校验凭证', 'json');
    if (!data?.RD) {showinbox("凭证解析异常：RD缺失，请登录");return null;}
    const dB = paswordAlgorithmsCookie(rd0 + rd1);
    return paswordAlgorithmsCookie(dB + data.RD);
};
// 这是中兴官方锁频接口，后台需处于登录状态。在左下方输入框输入频段，然后在f12控制台输入ztebandlock();就能调用。输入频段值建议为十六进制字符串(带有0x前缀)。计算方法：频段值N，2^(N-1)，再转成十六进制，如B3则2^2=4, 十六进制为0x4。多个频段值将它们的十六进制值相加就行了，全选为0x1E200000095
window.ztebandlock = async () => {
    const band = cmdContainer.value.trim();
    if (!band) return showinbox("请输入频段数值！");
    const ADtoken = await getAD();
    if (!ADtoken) return;
    const res = await safeFetch('/goform/goform_set_cmd_process', {
        method: 'POST',
        body: new URLSearchParams({
            isTest: 'false', 
            goformId: 'SET_NETWORK_BAND_LOCK', 
            lte_band_lock: band, 
            AD: ADtoken 
        })}, '中兴官方锁频');
    if (res) {showinbox("锁定成功, 稍等生效。");await resetmodem();}
};
const cmdContainer = document.getElementById('cmdInput')
window.evalcmd = async (cmd) => {
    const finalCmd = cmd || cmdContainer.value.trim();
    if (!finalCmd) return showinbox("请输入命令!");
    if (!cmd) {showinbox(`正在执行：${finalCmd} ...`);}
    const text = await safeFetch('/cgi-bin/upload/shell', {
        method: 'POST',
        body: finalCmd,
        headers: {'Content-Type': 'text/plain'}
    }, '指令执行', 'text');
    if (text !== null) if (!cmd) {showinbox(text)};return text;
};
const resultBox = document.getElementById('resultBox');
window.showinbox = (res) => {resultBox.textContent = res;};
let checkboxes = null;
const container = document.getElementById('band-container');
const button = document.getElementById('band-btn');
window.refreshband = async () => {
    const res = await evalcmd('at "AT+ZLTEBAND?"');
    if (!res) return;
    const match = res.match(/^_(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+)_$/m);
    if (!match) return alert(`频段解析失败：${res}`);
    const bytes = new Uint8Array(9);
    for (let i = 0; i < 9; i++) {bytes[i] = match[i + 1] | 0;}
    for (const cb of checkboxes) {
        const bit = cb._bitPos;
        cb.checked = (bytes[bit >> 3] >> (bit & 7)) & 1;
    }
};
window.showband = async () => {
    if (container.dataset.loaded) {
        const isHidden = container.style.display === 'none';
        container.style.display = isHidden ? 'block' : 'none';
        button.textContent = isHidden ? '收起面板' : '频段选择';
        if (isHidden) refreshband();return; 
    }
    const html = await safeFetch('./tmpl/bandlock.html', {}, '频段面板模板', 'text');
    if (!html) {container.innerHTML = '<span style="color:red;">请检查 ./tmpl/bandlock.html 或网络状态。</span>';return;}
    container.innerHTML = html;
    checkboxes = container.querySelectorAll('input');
    checkboxes.forEach(cb => {cb._bitPos = (cb.value - 1) | 0;});
    container.dataset.loaded = "1";
    container.style.display = 'block';
    button.textContent = '收起面板';
    refreshband();
};
window.lockSelectedBands = async () => {
    if (!confirm("确定锁定勾选的频段吗？")) return;
    const bytes = new Uint8Array(9);
    for (const cb of checkboxes) {
        if (!cb.checked) continue;
        const bit = cb._bitPos;
        bytes[bit >> 3] |= (1 << (bit & 7));
    }
    const res = await evalcmd(`at AT+ZLTEBAND=${bytes.join(',')}`);
    if (!res || res.includes("_ERROR_")) {return alert(`锁定失败：${res}`);}
    showinbox("锁定成功, 正在重启网络...");
    await resetmodem(); 
    refreshband();
};
const sleep = ms => new Promise(r => setTimeout(r, ms));
window.resetmodem = async () => {
    await evalcmd('at "AT+CFUN=0"');
    await sleep(500);
    await evalcmd('at "AT+CFUN=1"');
    showinbox("网络已重启。");
};
window.ADB = async (option) => {
    if (option !== '1' && option !== '0') return;
    const act = (option === '1') ? "开启" : "关闭";
    if (!confirm(`确定${act}ADB？`)) return;
    const res = await safeFetch('/goform/goform_set_cmd_process', {
        method: 'POST',
        body: new URLSearchParams({ goformId: 'SET_DEVICE_MODE', debug_enable: option })
    }, '开关ADB', 'json');
    if (res) showinbox(`操作成功！已发送${act}指令，设备即将重启。`);
};
window.setDefaultBand = async () => {
    if (!confirm('重置频段（全选所有频段）吗？')) return;
    const res = await evalcmd('at AT+ZLTEBAND=');
    if (!res || res.includes("_ERROR_")) return alert(`恢复默认失败：${res}`);
    showinbox("重置成功, 正在重启网络...");
    await resetmodem();
    refreshband();
};