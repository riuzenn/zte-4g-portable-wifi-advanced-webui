# zte-4g-portable-wifi-advanced-webui
中兴4G随身WiFi全功能后台 / A full-featured WebUI for ZTE 4G Mifi  
本工具目前只在f30a pro上测试过，其他设备请自行适配！！！  
[123网盘备份](https://www.123pan.com/s/NV4Qjv-IZYvd)  
### 开启adb  
http://192.168.0.1/goform/goform_set_cmd_process?goformId=SET_DEVICE_MODE&debug_enable=1  
### 关闭adb  
http://192.168.0.1/goform/goform_set_cmd_process?goformId=SET_DEVICE_MODE&debug_enable=0  
## § 关于adb的小知识
adb push支持中文字符和空格，用引号包裹整个路径即可  
直接从文件管理器拖动文件到cmd终端，自动填写路径  
## § 避免adb push /etc/rc后忘加执行权限导致砖机的可能办法  
◉往/etc/inittab（644权限）的最开头加三行    
```
::sysinit:mount -o remount,rw /dev/root /
::sysinit:/bin/chmod 755 /etc/rc
::sysinit:mount -o remount,ro /dev/root /
```
命令会在每次开机时重置权限。这个办法我试过可行（mount确认根目录是/dev/root这个设备路径，chmod 711 /etc/rc，reboot看权限有没有改成755），但是不推荐，万一这个文件里命令有错误就砖机了！！！  

我更推荐以下三个办法：  
◉往/etc/rc最后添加  
```
mount -o remount,rw /
chmod +x /opt/mybin/mods.sh
mount -o remount,ro /
/opt/mybin/mods.sh &
```
以后自定义开机命令都在/opt/mybin/mods.sh里添加。  

◉使用修改过二进制数据的adbd，adb push后的文件默认0755权限：  
下载修改版的[adbd](./bin/adbd)  
```
adb shell mount -o remount,rw /
#备份原版adbd
adb shell mv /bin/adbd /bin/adbd.bak
adb push adbd在你电脑上的位置 /bin
adb shell chmod 755 /bin/adbd
adb shell mount -o remount,ro /
```
修改原理：  
```
文件偏移    原始字节（小端）     修改为  
0x479e      c0 f3 08 08        40 f2 ed 18  
原本是ubfx r8, r0, #0, #9，改为movw r8, #493 ; 0x1ed，强制寄存器r8 = 0x1ed（即 0755）。无论adb客户端传了啥权限值，都只用固定的0755  
0x483e      46 ea 08 08        00 bf 00 bf  
原本orr.w r8, r6, r8把owner的权限赋给group，r8会变成0775，改为nop,nop（无操作），防止r8被修改  
0x486c      32 46              42 46  
原本mov r2, r6改为mov r2, r8，直接使用固定的r8的值作为最终权限（这里r6是两次提权的结果，数值是0777）
```
◉手动确认rc权限  
```
adb push rc在你电脑上的位置 /etc/rc  
#下面这两个命令一定确保执行，不然重启设备不能初始化会软砖！！！  
#这个命令给rc执行权限，adb push的文件默认没有执行权限  
adb shell chmod 755 /etc/rc  
#确认结果为-rwxr-xr-x开头  
adb shell ls -l /etc/rc
```
## § 自定义可执行文件和配置文件放哪  
我倾向于/opt/mybin和/opt/myconf，但是busybox硬编码了PATH=/sbin:/usr/sbin:/bin:/usr/bin，又不想每次以全路径调用可执行文件。所以我决定接下来将可执行文件放/usr/sbin，因为4个路径里这里文件最少。sh脚本和配置文件放/opt/myconf。  
## § 利用原生CGI在web后台执行shell命令
### ➤下载:  
[shell](./etc_ro/cgi-bin/shell)  
[index.html](./etc_ro/web/index.html)  
[customfuncs.js](./etc_ro/web/js/customfuncs.js)  
### ➤adb执行： 
```
adb shell mount -o remount,rw /  
adb shell mkdir -p /etc_ro/cgi-bin/  
adb push shell在你电脑上的位置 /etc_ro/cgi-bin/shell  
adb shell chmod 755 /etc_ro/cgi-bin/shell  
adb push index.html在你电脑上的位置 /etc_ro/web/index.html  
adb shell chmod 755 /etc_ro/web/index.html  
adb push customfuncs.js在你电脑上的位置 /etc_ro/web/js/customfuncs.js  
adb shell chmod 755 /etc_ro/web/js/customfuncs.js
adb shell mount -o remount,ro /  
```
### ➤原理：  
◉反编译/bin/goahead可知f30a pro是支持cgi-bin的，硬编码了路径为/etc_ro/cgi-bin，只识别来自`http://192.168.0.1/cgi-bin/upload/`的命令请求。  
◉浏览器post了一个请求后，goahead将body的内容复制到`/var/cgi*（*表示随机的一串字符）`，并将这个文件的路径赋值给$UPLOAD_FILENAME。我们要做的就是从$UPLOAD_FILENAME读取命令然后执行（eval）。  
◉/var路径挂载到硬盘不是运行内存，所以`/var/cgi*`重启还在，好在goahead会自动删除。少数情况如执行reboot后，goahead来不及删除，需要我们在/etc/rc开机脚本里添加`rm -f /var/cgi*`。  
◉如图修改/bin/goahead的十六进制值将/var改为/tmp（tmp挂载到运行内存），就没有`/var/cgi*`文件残留的问题，因为重启后内存就会重置，/etc/rc里也不用加代码。我已经改好，下载推送设置权限即可[goahead](./bin/goahead)。  
<div align="center"><img src="./images/路径var改为tmp.jpg"></div>

### ➤已知bug:  
◉ls /var会比正常结果多一个`cgi*`，这是正常的，因为咱们靠`cgi*`文件工作，该文件在命令执行后就会删除。  
◉千万不要`cat /var/cgi*`，`cgi*`会发疯似的扩容到占满硬盘，具体原理不清楚。  
◉如果替换了我修改的goahead，千万不要`cat /tmp/cgi*`，内存应该会撑爆。  
### ➤备注：  
◉受限于实现原理，每次fork出的shell进程执行过一次命令就会销毁，想一次执行多个命令建议用“;”分割,，比如说`ls /;ls /etc`  
◉不过值得注意的安全隐患是我没加校验，网络攻击者可以很轻易地执行shell命令，建议apn里不要启用ipv4v6。  
◉customfuncs.js封装了getAD();、evalcmd();等js函数。getAD();用于post某些goahead原生命令需要AD参数的情况；evalcmd();工作原理就是上面提到的，可以用于自定义链接，如`<a href="javascript:void(0);" onclick="confirm('即将执行XX命令'); evalcmd('这里写shell命令如reboot');">我是重启</a>`。  
◉index.html下面这一栏“退出”是退出登录；"重启"字面意思（值得一提的是goahead原生提供了REBOOT_DEVICE接口，但是需要处于登录状态，所以重启我调用的是evalcmd('reboot');。  
## § 中兴随身wifi全功能后台的最后一块拼图--at工具
我编译了一个可执行文件，可以调用这个工具在命令行执行at命令。工具参考了官方zte_mifi、libatutils.so和atweb的反编译代码，在这里向包括mWIFI_icu、棒子版煤油等在内的前辈表示感谢。  
<div><img src="./images/at工具示例.jpg"  style="width: 600px; height: auto;"></div>

### ➤编译:  
我编译了适配官方3.4.110内核和uClibc 0.9.33.2库的Buildroot交叉编译器（详情见 https://github.com/riuzenn/zte-4g-portable-wifi-gcc-and-dynamically-linked-binaries ），用[Makefile](https://github.com/riuzenn/zte-4g-portable-wifi-gcc-and-dynamically-linked-binaries/blob/main/编译命令/at/Makefile)编译at工具，文件里的具体路径根据实际情况自行修改。值得一提的是，我在Makefile里加入了大部分可用编译优化命令，可以尝试移植到其他二进制文件的编译命令里。  
编译at  
创建并转到文件夹：`mkdir -p ~/at_build;cd ~/at_build`  
写好Makefile里的绝对路径后上传Makefile、at.c到`~/at_build`    
编译：`make`  
编译失败重置：`make clean`  
### ➤安装:  
下载[at](./usr/sbin/at)  
```
adb shell mount -o remount,rw /  
adb push at文件在你电脑上的路径  /usr/sbin/at  
adb shell chmod 755 /usr/sbin/at
adb shell mount -o remount,ro /  
```
### ➤原理：  
官方封装了一套和at串口通信的方法：goahead接受前端url，zte_mifi把守大门（阻塞了几个疑似modem的串口），向底层提交申请然后排队执行。我写的这个c程序就是调用重写后的官方send_req_and_wait函数，发送at命令，接收返回值。和已有的atwed的区别在于atweb开了个端口持续监听，需要后台运行，并且把大多数逻辑写进了编译后的文件，是一个小型的服务器。我这个工具只在命令行调用的时候运行，全功能后台主要靠js实现，性能可能比编译后的c程序好，因为js由访问后台的电脑和手机执行，而c程序在性能孱弱的随身wifi运行，占用总共约32MB的运行内存的一部分。  
### ➤为什么要重复造轮子？  
用十六进制查看atweb就能发现它里面封装了收集包括imei、iccid等在内的信息然后和一串加密字符串拼接成url检测是否付费的函数，再加上atweb有很高权限，所以我才花时间把这个小东西写出来，并且附上源码[at.c](https://github.com/riuzenn/zte-4g-portable-wifi-gcc-and-dynamically-linked-binaries/blob/main/源码/at/at.c)，感兴趣可以自己编译。一切代码都是明文，我可以保证我提交的代码没有后台。  
### ➤已知bug（已修复）：  
输出包含过多底层日志，这是因为过程涉及复杂函数调用，每个都会拉点屎。可以在源码里屏蔽了。我本着够用就行的原则没管。  
<div><img src="./images/底层日志.jpg"  style="width: 600px; height: auto;"></div>  

更新！！！我重写了libatutils库里的几个函数，彻底不打印无关日志。受cvghh@酷安启发，用第二个参数控制输出格式，为1时打印`_返回字符串_`方便正则匹配。  
<div><img src="./images/at工具示例2.jpg" style="width: 600px; height: auto;"></div>  

### ➤小设计：  
成功输出_at串口返回值_  
非查询类at命令成功执行输出_OK_  
失败输出_ERROR_  
用正则表达式`/^_(我是要匹配的内容)_$/m`能轻松匹配。  
### ➤基于at工具实现的功能:  
◉首先需要下载推送以下文件，如有定制化需求自行适配。  
/etc_ro/cgi-bin/shell：通过post请求执行shell命令，一切的基础  
/usr/sbin/at：主角，命令行执行at命令用  
/etc_ro/web/index.html：后台主界面，我在上面加了很多蓝色功能键  
/etc_ro/web/js/customfuncs.js：我写的大部分js函数都在里面  
/etc_ro/web/tmpl/bandlock.html：插入主界面的锁频面板  
/etc_ro/web/tmpl/status/device_info.html：设备信息页面添加当前频段和签约速率，我没有加定时刷新的代码，信息变化后要手动刷新页面  
◉锁频面板：at+zlteband=逗号分割的9组数字  
<div align="center"><img src="./images/锁频关.jpg"></div>  
<div align="center"><img src="./images/锁频开.jpg"></div>  

◉设备信息页面添加当前频段和签约速率：AT+ZBAND?和AT+CGEQOSRDP=1  
<div align="center"><img src="./images/信息页面.jpg"></div>  

改串和锁小区等功能我用不到所以没在网页上加按钮。既然有了at工具可以自己在左下角的输入框执行AT命令，加at 前缀即可。  
◉查询及设置IMEI即串号  
AT+CGSN  
AT+MODIMEI=  
◉查看及锁小区  
AT+ZLC?  
AT+ZLC=  
格式：0(非锁定状态)或1(锁定状态),频点,小区  
◉查看及修改无线的mac地址  
AT+MAC?  
AT+MAC=  
### ➤官方锁频接口  
值得一提的是官方goahead留了锁频接口，但是没给网页前端入口。我把实现方法写入了customfuncs.js，感兴趣的可以试试。这个接口好在goahead已经编译相关代码，我们只需要写好前端js和按钮就好。坏处是要登录，调用过程繁琐。  
<div align="center"><img src="./images/官方锁频接口.jpg"></div>  

## § 其他功能
### ◉自动APN设为IPv4v6  
下载[auto_apn.db](./etc_ro/config/auto_apn/auto_apn.db)推送到/etc_ro/config/auto_apn/auto_apn.db  
f30a pro的这个数据库文件默认为IP，我把数据库里国内运营商的APN都设为IPv4v6，想用IPv4使用手动APN。  
<div align="center"><img src="./images/apn.jpg"></div>  

### ◉开机后关闭白色led灯（推送完rc记得chmod 755，不然重启后会砖）
在/etc/rc里添加如下代码，sleep后面的数值是暂停的秒数：  
(sleep 10;echo 0 > /sys/class/leds/modem_w_led/brightness) &  
### ◉更严格的防火墙规则  
下载[ipv4v6_firewall.sh](./opt/myconf/ipv4v6_firewall.sh)  
在/etc/rc里添加如下代码：  
/opt/myconf/ipv4v6_firewall.sh  
```
adb shell mount -o remount,rw /
adb shell mkdir -p /opt/myconf
adb push ipv4v6_firewall.sh在电脑的路径 /opt/myconf/ipv4v6_firewall.sh
adb shell chmod 755 /opt/myconf/ipv4v6_firewall.sh
adb shell mount -o remount,ro / 
```
### ◉修改nv默认设置（推送完记得chmod 755，不然恢复出厂后会砖）  
[default_parameter_sys](./etc_ro/default/default_parameter_sys)文件中  
cdrom_state=0和usb_devices_debug=diag,adb,serial（删掉mass_storage），adb开启状态不会加载CDROM设备（设备管理器和我的电脑不会显示cd设备）  
[default_parameter_user](./etc_ro/default/default_parameter_user)文件中  
need_support_sms=yes，f30a pro开启短信功能  
admin_Password=，设置默认密码，sha256加密  
privacy_read_flag=1，关闭重置后的隐私协议弹窗  
dm_update_mode=0，默认关闭自动检测新版本  
HideSSID=1，默认隐藏wifi名  
wifi_11n_cap=0，wifi默认频宽设为20MHz  
### ◉自动计算填入切卡密码  
下载[unclock_sim.html](./etc_ro/web/tmpl/adm/unclock_sim.html)，推送到/etc_ro/web/tmpl/adm/unclock_sim.html  
中兴工程师取文件名时写错英语单词了，正确文件名应该拼写为unlock_sim.html。如果要为其他IMEI计算切卡密码也可以手动输入然后点计算。  
<div align="center"><img src="./images/自动计算切卡密码.jpg"></div>  

### ◉测试内核是否支持硬件浮点  
下载[fpu_test.c](./源码/fpu_test.c)，按如下命令编译  
```
$HOME/usr/bin/arm-buildroot-linux-uclibcgnueabi-gcc \
    -Os \
    -march=cortex-a53 -mtune=cortex-a53 \
    -mfloat-abi=softfp \
    -mfpu=vfp \
    fpu_test.c \
    -o fpu_vfp
```
发现一执行到VFP指令集就退出，报错Illegal instruction，换成-mfloat-abi=soft能正常输出结果，说明内核不支持硬件浮点，但是库文件里可以搜到硬件浮点指令，这一点很割裂。  
### ◉修改下发DNS和随身wifi自身DNS为阿里DNS  
```
adb shell nv set dhcpDns="223.5.5.5 223.6.6.6"  
adb shell nv set DNS_proxy=disable  
adb shell nv save
```
下载[resolv.conf](./opt/myconf/resolv.conf)推送到/opt/myconf/resolv.conf  
在rc里添加  
mount --bind /opt/myconf/resolv.conf /etc/resolv.conf  
killall dnsmasq  
### ◉内核优化参数  
下载[sysctl.conf](./opt/myconf/sysctl.conf)推送到/opt/myconf/sysctl.conf  
rc中添加  
sysctl -qp /etc/sysctl.conf  
主要是内核级地禁用了ipv6，并激进地杀掉结束的或长时间不响应的链接来减少内存占用。  
### ◉修改后台网页标题、修改后台网页图标为蓝字ZTE、透明底的网页标签图标  
修改/etc_ro/web/js/config/ufi/mf93d/config.js里的WEBUI_TITLE:"4G Mobile Hotspot"，修改引号里的内容为自定义字符串。  
下载[favicon.ico](./etc_ro/web/favicon.ico)，推送到/etc_ro/web/favicon.ico。  
<div align="center"><img src="./images/更改后台网页标题.png"></div>  

### ◉显示所有接入设备的名称和物理地址  
下载[home.html](./etc_ro/web/tmpl/home.html)，推送到/etc_ro/web/tmpl/home.html  
点查看就能看到。  
ip neigh show结果中REACHABLE是处于连接状态的设备。  
dumpleases -f /etc_rw/udhcpd.leases包括所有连接过的设备，但是当前不一定在线。  
两个结合一下就能得出当前在线的所有设备。但是目前我不知道如何很好地区分wifi和rndis设备。  
另外js提取一下字符串，再加一个官方风格的`<table>`标签，会和谐一点，但是我懒，凑活看吧。  
<div align="center"><img src="./images/显示所有接入设备的名称和物理地址.png"></div>  

### ◉快速开机不适用中兴随身wifi棒子  
往/etc_ro/web/js/config/ufi/mf93d/menu.js添加以下代码，在设置-设备设置里会多出快速开机，真的会快一点吗？我没感觉出来。  
```
,{
  hash: "#fastboot",
  path: "adm/fastboot",
  level: "3",
  parent: "#device_setting",
  requireLogin: a,
  checkSIMStatus: false
}
```
从通电到设备变白灯这段时间不会因为快速开机开关改变，都是20s左右。我看了一下service.js文件负责传递网页上用户选是还是否（mgmt_quicken_power_on，/etc_ro/default/default_parameter_user有这个flag，默认为0），传给goahead，后者写入nv。zte_mifi读取nv设置值，执行相应逻辑。zte_mifi里面藏着相关逻辑。开启了mgmt_quicken_power_on，设备并不会真正关机，而是进入低功耗模式，我觉得类似电脑睡眠，设备并没有真正关机断电。按下电源键开机后只是把设备唤醒，自然快咯。棒子没有电源键也没电池，所以这个功能对于棒子来说没用，所以隐藏了。  
<div align="center"><img src="./images/快速开机设置.jpeg"></div>  

### ◉登录机制  
http://192.168.0.1/goform/goform_get_cmd_process?isTest=false&cmd=LD  
得到json格式的LD登录凭证  
{"LD":"427C02FDC13A7F4842703C2081DC56070572422BD398AD411B6A00C34EAE5267"}  
最终密码=sha256加密((sha256加密明文密码，结果转大写字母)拼接LD字符串)，结果转大写字母  
http://192.168.0.1/goform/goform_set_cmd_process?isTest=false&goformId=LOGIN&password=最终密码  
备注：  
LD不是常量，反编译goahead发现LD是其根据时间型号等信息生成的sha256值，且会为每个未登录的 IP 分配一个临时的 LD，如果前一个 LD 还没有被“消耗”（即还没有进行过一次失败或成功的 LOGIN POST），后端程序为了节省计算资源，会返回同一个LD值。  
原版逻辑里第二个链接是通过post方式提交，实测直接访问链接或者说get方式也行。  
### ◉减少打印日志行为  
#### ➤修改  
1. /etc_ro/default/default_parameter_sys中：  
dnsmasqfileSize=1024改为0  
errnofileSize=1024改为0  
hotplugfileSize=1024改为0  
mynetlinkfileSize=1024改为0  
print_level=2改4  
syslog_level=本来就是4  
2. /etc_ro/default/default_parameter_user中：  
comm_logsize=16384改小点如1024  
3. /bin/hostapd的90768偏移地址的03改为05  
4. /sbin/zte_mifi  
80E98偏移地址的-dddd改为-qqqq  
85754偏移地址的-d改为-t（影响hostapd的启动命令）  
86036偏移地址开始：  
logger_syslog=这里有个空格8改为0换行，换行的十六进制编码是0A  
logger_syslog_level=2改为5  
logger_stdout=8改为0  
logger_stdout_level=2改为5  
#### ➤原理  
反编译libsoftap.so，从file_write函数可知：  
若`*fileSize`为0直接不打印，若为空则将comm_logsize的值赋给它们。  
反编译libsoftap.so，从loglevel_init、slog、log_sig_hdl函数可知：  
内部消息级别：1=debug、2=notice、3=error。  
print_level和syslog_level二者有效值1-4，其余值会被重置为4，越高日志越少，4等于全关。  
反编译hostapd可知：  
logger_syslog和logger_stdout是模块掩码，改成0则全部模块的日志都不打印。1=IEEE 802.11, 2=IEEE 802.1X, 4=RADIUS, 8=WPA, 0x10=DRIVER, 0x40=MLME，0x7F（127）及以上 / -1所有。  
logger_syslog_level和logger_stdout_level是级别阈值，消息级别超过阈值才输出，5全关。  
还有日志不受上面四个变量管，直接和hostapd的90768偏移地址的值比较，消息级别超过这个值才打印，5是error档。  
## § 编译的其他应用  
除了at外，我还编译了一些实用的工具。详情见https://github.com/riuzenn/zte-4g-portable-wifi-gcc-and-dynamically-linked-binaries  
### ◉简单介绍一下dropbear  
#### ➤安装过程：
编译好的[dropbearmulti](./usr/sbin/dropbearmulti)连同[sshon](./usr/sbin/sshon)和[sshoff](./usr/sbin/sshoff)推送到/usr/sbin，[index.html](./etc_ro/web/index.html)和[customfuncs.js](./etc_ro/web/js/customfuncs.js)推送到/etc_ro/web和/etc_ro/web/js，执行：  
```
adb shell
mount -o remount,rw /
chmod 755 /usr/sbin/dropbearmulti
chmod 755 /usr/sbin/sshon
chmod 755 /usr/sbin/sshoff
ln -s /usr/sbin/dropbearmulti /usr/sbin/dropbear
ln -s /usr/sbin/dropbearmulti /usr/sbin/dropbearkey
#生成dropbear服务器端密钥
mkdir -p /etc/dropbear
dropbearkey -t ed25519 -f /etc/dropbear/dropbear_ed25519_host_key
#查看生成的dropbear服务器公钥
dropbearkey -y -f /etc/dropbear/dropbear_ed25519_host_key
```
#### ➤启动和连接dropbear  
我写了sshon和sshoff，直接输入它们的文件名就可以开启和关闭。windows的cmd里输入ssh admin@192.168.0.1即可连接。  
若密码错误或想以密钥登录参考https://github.com/riuzenn/zte-4g-portable-wifi-gcc-and-dynamically-linked-binaries  
<div align="center"><img src="./images/包含ssh的index.jpg"></div>  

### ◉websh和wssh  
二者服务于在网页浏览器里执行shell命令的需求。  
websh原理和goahead自带cgi一样，都是一次命令fork一个sh进程。  
安装：[websh](./usr/sbin/websh)→/usr/sbin，[websh.html](./etc_ro/web/websh.html)→/etc_ro/web，chmod 755  
启动：服务器端websh &，客户端访问http://192.168.0.1:2333 (/websh.html可不写)  
关闭：服务器端killall websh 
<div align="center"><img src="./images/websh.jpg"></div>  

wssh缩写自websocket shell，是真正的实时交互式shell，是不是有cloud shell那味了？  
安装：  
[wssh](./usr/sbin/wssh)→/usr/sbin  
[wssh.html](./etc_ro/web/wssh.html)→/etc_ro/web  
[xterm.min.css](./etc_ro/web/css/xterm.min.css)→/etc_ro/web/css  
[xterm.min.js](./etc_ro/web/js/xterm.min.js)→/etc_ro/web/js  
[xterm-addon-fit.min.js](./etc_ro/web/js/xterm-addon-fit.min.js)→/etc_ro/web/js  
也可以去以下网址下载  
https://cdn.jsdelivr.net/npm/xterm@5.3.0/css/xterm.min.css  
https://cdn.jsdelivr.net/npm/xterm@5.3.0/lib/xterm.min.js  
https://cdn.jsdelivr.net/npm/@xterm/addon-fit@0.10.0/lib/addon-fit.min.js  
chmod 755  
启动：服务器端wssh &，客户端访问http://192.168.0.1:2333 (/wssh.html可不写)  
关闭：服务器端killall wssh  
<div align="center"><img src="./images/wssh.jpg"></div>  
