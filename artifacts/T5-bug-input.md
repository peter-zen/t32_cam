# T5 — Bug/需求输入 (raw + 分析)

## 用户描述
第四次启动 `htc_main_app -m` 时出现 `logs/debug1.log`,内核 oops。用户给出根治方向:
让 WiFi 驱动和网络连接**跨 APP 重启持久化复用**——APP 只在驱动没加载时才 insmod,
只在没联网时才走连接流程,退出时只收尾 APP 自己、绝不碰 WiFi 配置/不卸载驱动。

## 第四次现象(内核 oops,非用户态卡死)

`logs/debug1.log`:启动到 `OSDController: setPoolSize(2) done`(L53)后出现内核 oops:
- `ExcCode 0b` = **协处理器不可用(CP Unusable)**,发生在内核态(EXL=1)→ 控制流走飞。
- `epc: exit_robust_list` / `ra: mm_release` —— 崩在**进程退出路径**的 robust futex 链表清理。
- 崩溃进程 `Comm: DbusProcess`(WiFi/dbus),**不是** htc_main_app。
- `Tainted: G W O` —— 外部/私有内核模块(8189fs WiFi 驱动等)。
- 经典解读:robust futex 链表被踩坏 → 内核内存损坏 → oops。与反复创建销毁的 dbus/wpa 进程有关。

## 与前三次、T2/T3 的关系
- 前三次(debug.log)是**用户态** RTSP/IMP configure 卡死,T2/T3 已修。
- 第四次是**内核态** oops,崩溃进程 DbusProcess,**与 T2/T3 的 IMP teardown 无因果关系**。
- 触发源:每次启动都重 `insmod 8189fs.ko`(报 `File exists`)+ 重 spawn `wpa_supplicant` &
  (撞残留 `/tmp/wpa_supplicant` ctrl socket,报 `ctrl_iface exists`)。wpa_supplicant 后台 spawn
  且退出从不清理,反复创建销毁累积出 dbus 内核态 robust-futex 损坏,第4次 oops。

## 现状根因(代码层)
- `Misc::connectWifi`(Misc.cpp:401)用**进程内静态标志** `already_inited_wifi`(Misc.cpp:26/.h:50)
  守卫 insmod → 进程重启清零 → 每次重 insmod(`File exists`)。
- connectWifi 每次重 spawn wpa_supplicant(无状态判断)→ 与残留 ctrl socket 冲突。
- 退出(performCleanup + main_exit)**从不碰 WiFi**(已确认),符合用户"退出不动 WiFi"。

## 用户批准的方案(详见 ~/.claude/plans/joyful-stirring-starlight.md)
状态驱动探测:启动时查驱动是否真在内核、网络是否真已连,按实际状态决定。退出层保持现状+注释。
