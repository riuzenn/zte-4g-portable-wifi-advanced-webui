const errorhandler = (xhr, status, error) => {
    if (xhr.status === 0) {showinbox('设备连接已断开。');}
    else {showinbox(`状态码：${xhr.status} 状态描述：${status}  错误描述：${error} `);}
};
// AD = Hash(Hash(rd0 + rd1) + RD), 逻辑参考自service.js，生成goform接口校验参数AD
window.getAD = (callback) => {
    $.getJSON('/goform/goform_get_cmd_process', {cmd: 'RD'}).done((res) => {
        const dB = paswordAlgorithmsCookie(rd0 + rd1);
        const AD = paswordAlgorithmsCookie(dB + res.RD);
        callback(AD);
    }).fail(errorhandler);
};
window.ztebandlock = () => {
// 这是中兴官方锁频接口，后台需处于登录状态。在左下方输入框输入频段，然后在f12控制台输入ztebandlock();就能调用。输入频段建议为十六进制字符串(带有0x前缀)，十进制也可以。计算方法：频段值N，2^(N-1)，再转成十六进制，如B3则2^2=4, 十六进制为0x4。多个频段值将它们的十六进制值相加就行了，全选为0x1E200000095
    const band = cmdContainer.value.trim();
    getAD((ADtoken) => {
        $.post('/goform/goform_set_cmd_process', {isTest: 'false', goformId: 'SET_NETWORK_BAND_LOCK', lte_band_lock: band, AD: ADtoken, }).done((res) => {showinbox("锁定成功, 稍等生效。");resetmodem();}).fail(errorhandler);});
};
const cmdContainer = document.getElementById('cmdInput')
window.evalcmd = (cmd) => {
    if (cmd) {return $.post('/cgi-bin/upload/shell', cmd).fail(errorhandler);}
    else {
        const cmdInput = cmdContainer.value.trim();
        if (!cmdInput) {showinbox("请输入命令");return;}
        showinbox(`正在执行：${cmdInput} ...`);
        $.ajax({
            url: '/cgi-bin/upload/shell',
            type: 'POST',
            data: cmdInput,
            contentType: 'text/plain',
            success: showinbox,
            error: errorhandler,
        });}
};
const resultBox = document.getElementById('resultBox');
window.showinbox = (res) => {resultBox.innerText = res;
};
const container = document.getElementById('band-container');
const button = document.getElementById('band-btn');
window.refreshband = () => {
    evalcmd('at "AT+ZLTEBAND?"').done((res) => {
        const match = res.match(/^_(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+)_$/m);
        if (!match) { alert(`频段解析失败：${res}`); return; }
        const byteArr = match.slice(1).map(Number);
        const checkboxes = container.querySelectorAll('input');
        for (const cb of checkboxes) {
            const bit = (cb.value - 1) | 0;
            const byte = byteArr[bit >> 3];
            if (byte === undefined) continue;
            cb.checked = (byte >> (bit & 7)) & 1; 
        }
    });
};
window.showband = () => {
    if (container.dataset.loaded) {
        const isHidden = container.style.display == 'none';
        container.style.display = isHidden ? 'block' : 'none';
        button.textContent = isHidden ? '收起面板' : '频段选择';
        if (isHidden) {refreshband();}return;
    }
    $.get('./tmpl/bandlock.html').done((html) => {
        container.innerHTML = html;
        container.dataset.loaded = "1";
        container.style.display = 'block';
        button.textContent = '收起面板';
        refreshband();
    }).fail(() => {container.innerHTML = '<span style="color:red;">请检查./tmpl/bandlock.html或网络状态。</span>';});
};
window.lockSelectedBands = () => {
    const byteArr = [0, 0, 0, 0, 0, 0, 0, 0, 0];
    const checkboxes = container.querySelectorAll('input');
    for (const cb of checkboxes) {
        if (!cb.checked) continue;
        const bit = (cb.value - 1) | 0;
        byteArr[bit >> 3] |= (1 << (bit & 7));
    }
    const bandListStr = byteArr.join(',');
    if (!confirm("确定锁定勾选的频段吗？")) return;
    evalcmd(`at AT+ZLTEBAND=${bandListStr}`).done((res) => {
        if (res.includes("_ERROR_")) {alert(`锁定失败：${res}`);return;}
        showinbox("锁定成功, 正在重启网络...");resetmodem().then(refreshband);
    });
};
window.resetmodem = () => {
    return evalcmd('at "AT+CFUN=0"')
        .then(() => new Promise(resolve => setTimeout(resolve, 1000)))
        .then(() => evalcmd('at "AT+CFUN=1"'))
        .then(() => {showinbox("网络已重启。");});
};
window.ADB = (option) => {
    if (option !== '1' && option !== '0') return;
    const act = (option === '1') ? "开启" : "关闭";
    if (!confirm(`确定${act}ADB？`)) return;
    $.post('/goform/goform_set_cmd_process', {goformId: 'SET_DEVICE_MODE', debug_enable: option}).done((res) => {showinbox(`${res}已${act}ADB，即将重启`)}).fail(errorhandler);
};
window.setDefaultBand = () => {
    if (!confirm('重置频段（全选所有频段）吗？')) return;
    evalcmd('at AT+ZLTEBAND=').done((res) => {
        if (res.includes("_ERROR_")) {alert(`恢复默认失败：${res}`);return;}
        showinbox("重置成功, 正在重启网络...");resetmodem().then(refreshband); //
    });
};